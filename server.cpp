#include "server.h"
#include <toml.hpp>

VideoServer::VideoServer(QString name, quint16 port, QObject *parent)
    : QObject(parent), name(name)
{

    auto data = toml::parse(CONFIG_FILE.toStdString());
    savePath = QString::fromStdString(toml::find<std::string>(data,"setting","video_path"));

    ffmpeg = new QProcess(this);

    // video socket new connection event
    connect(&m_server, &QTcpServer::newConnection,
            this, &VideoServer::onNewConnection);

    if (!m_server.listen(QHostAddress::Any, port))
        Writter::error("Stream server listen failed.");
    else
        Writter::info(QString("Single socket server %1 listening on port %2")
                      .arg(name).arg(port));


    QDir().mkpath(savePath);
}

void VideoServer::sendToClient(const QString& videoPath, const QString& answer)
{
    QJsonObject obj;
    QString dirPath;
    QString dirName;

    int pos = videoPath.indexOf("_cam");
    if(pos != -1)
        dirPath = videoPath.left(pos);

    dirName = dirPath.split('/').last();

    // request write log
//    emit requestLog(dirName, answer, videoPath);

    const QString requestId = takeRequestId(videoPath);
    QTcpSocket* socket = m_resultSockets.take(requestId);

    obj["fileName"] = dirName;
    obj["requestId"] = requestId;
    obj["success"] = true;
    obj["answer"] = answer;

    sendOrQueueResult(requestId,
                      socket,
                      VssProtocol::PacketType::Result,
                      obj);
}

void VideoServer::sendErrorToClient(const QString& videoPath,
                                    const QString& error)
{
    QJsonObject obj;
    const QString requestId = takeRequestId(videoPath);
    QTcpSocket* socket = m_resultSockets.take(requestId);

    obj["fileName"] = QFileInfo(videoPath).baseName();
    obj["requestId"] = requestId;
    obj["success"] = false;
    obj["error"] = error;

    sendOrQueueResult(requestId,
                      socket,
                      VssProtocol::PacketType::Error,
                      obj);
}

bool VideoServer::sendPacket(QTcpSocket* socket,
                             VssProtocol::PacketType type,
                             const QByteArray& payload)
{
    if (!socket ||
        socket->state() != QAbstractSocket::ConnectedState ||
        payload.size() > static_cast<int>(VssProtocol::MaxPayloadSize))
        return false;

    const QByteArray packet = VssProtocol::makePacket(type, payload);
    if (socket->write(packet) != packet.size())
        return false;

    socket->flush();
    return true;
}

void VideoServer::sendOrQueueResult(const QString& requestId,
                                    QTcpSocket* socket,
                                    VssProtocol::PacketType type,
                                    const QJsonObject& result)
{
    if (requestId.isEmpty()) {
        Writter::error(QString("Cannot route result on %1: empty requestId")
                       .arg(name));
        return;
    }

    const QByteArray payload =
            QJsonDocument(result).toJson(QJsonDocument::Compact);
    if (sendPacket(socket, type, payload)) {
        Writter::info(QString("Result sent on %1, requestId=%2")
                      .arg(name, requestId));
        return;
    }

    if (m_pendingResults.size() >= 100 &&
        !m_pendingResults.contains(requestId))
        m_pendingResults.erase(m_pendingResults.begin());
    m_pendingResults[requestId] = VssProtocol::makePacket(type, payload);
    Writter::warn(QString("Result queued on %1, requestId=%2")
                  .arg(name, requestId));
}

void VideoServer::flushPendingResult(QTcpSocket* socket,
                                     const QString& requestId)
{
    if (!socket || requestId.isEmpty() ||
        !m_pendingResults.contains(requestId) ||
        socket->state() != QAbstractSocket::ConnectedState)
        return;

    const QByteArray packet = m_pendingResults.value(requestId);
    if (socket->write(packet) == packet.size()) {
        socket->flush();
        m_pendingResults.remove(requestId);
        Writter::info(QString("Queued result flushed on %1, requestId=%2")
                      .arg(name, requestId));
    }
}

QString VideoServer::takeRequestId(const QString& videoPath)
{
    return m_requestIdsByVideoPath.take(videoPath);
}
VideoServer::~VideoServer()
{
    Writter::info("Terminating VSS_Server...");
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket *socket = it.key();
        ClientContext *ctx = it.value();


        delete ctx;
        if (socket) {
            socket->deleteLater();
        }
    }

    m_clients.clear();

}

bool VideoServer::ensureFfmpegRunning(QTcpSocket *socket, ClientContext *ctx)
{
    Q_UNUSED(socket);

    if (!ctx || ctx->requestId.isEmpty() ||
        ctx->sensorName.isEmpty() || ctx->camId.isEmpty())
        return false;

    if (ctx->videoStarted)
        return ffmpeg->state() == QProcess::Running;

    if (ffmpeg->state() == QProcess::Running)
        return false;

    const QString timestamp =
            QDateTime::currentDateTime().toString("yyyyMMdd_hhMMss");
    const QString fileName = ctx->sensorName + "_" + ctx->camId
            + "_" + timestamp + ".mp4";

    QString dirPath = QString("%1/%2").arg(savePath, fileName);

    QDir().mkdir(savePath);

    ctx->savePath = dirPath;

    ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);

    QStringList args;
    args << "-hide_banner"
         << "-loglevel" << "error"
         << "-y"
         << "-f" << "mpegts"
         << "-fflags" << "+genpts"
         << "-i" << "pipe:0"
         << "-c:v" << "copy"
         << "-movflags" << "+faststart"
         << ctx->savePath;

    ffmpeg->setProgram("ffmpeg");
    ffmpeg->setArguments(args);
    ffmpeg->start(QIODevice::ReadWrite);

    if(!ffmpeg->waitForStarted(30000))
    {
        Writter::warn("Fail to start ffmpeg");
        return false;
    }

    ctx->videoStarted = true;

    return true;

}

void VideoServer::stopFfmpeg()
{
    if(ffmpeg->state() == QProcess::NotRunning)
        return;

    ffmpeg->closeWriteChannel();
    if(ffmpeg->waitForFinished(60000))
        return;

    ffmpeg->kill();
    ffmpeg->waitForFinished();
}

void VideoServer::onNewConnection()
{
    while (m_server.hasPendingConnections())
    {
        QTcpSocket *socket = m_server.nextPendingConnection();

        auto *ctx = new ClientContext;
        m_clients.insert(socket, ctx);

        connect(socket, &QTcpSocket::readyRead, this, &VideoServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &VideoServer::onDisconnected);


        QString connectLog = QString("Client connected: %1 %2 localPort = %3")
                              .arg(socket->peerAddress().toString())
                              .arg(socket->peerPort())
                              .arg(socket->localPort());

        Writter::info(connectLog);
    }
}

void VideoServer::onReadyRead()
{
    QMutexLocker locker(&ffmpegMutex);
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if (!socket || !m_clients.contains(socket))
        return;

    ClientContext *ctx = m_clients.value(socket);
    if (!ctx)
        return;

    ctx->inputBuffer.append(socket->readAll());

    while (true)
    {
        if (ctx->inputBuffer.size() < VssProtocol::HeaderSize)
            return;

        const QByteArray header =
                ctx->inputBuffer.left(VssProtocol::HeaderSize);
        QDataStream stream(header);
        stream.setByteOrder(QDataStream::BigEndian);

        quint32 magic = 0;
        quint8 rawType = 0;
        quint32 payloadSize = 0;
        stream >> magic >> rawType >> payloadSize;

        if (magic != VssProtocol::Magic ||
            payloadSize > VssProtocol::MaxPayloadSize)
        {
            Writter::error(QString("Invalid protocol header from %1")
                           .arg(socket->peerAddress().toString()));
            socket->abort();
            return;
        }

        const int packetSize = VssProtocol::HeaderSize
                + static_cast<int>(payloadSize);
        if (ctx->inputBuffer.size() < packetSize)
            return;

        const QByteArray payload =
                ctx->inputBuffer.mid(VssProtocol::HeaderSize, payloadSize);
        ctx->inputBuffer.remove(0, packetSize);

        if (!processPacket(socket,
                           ctx,
                           static_cast<VssProtocol::PacketType>(rawType),
                           payload))
        {
            Writter::error(QString("Failed to process packet type %1 on %2")
                           .arg(rawType)
                           .arg(name));
            socket->abort();
            return;
        }
    }
}

bool VideoServer::processPacket(QTcpSocket* socket,
                                ClientContext* ctx,
                                VssProtocol::PacketType type,
                                const QByteArray& payload)
{
    if (!socket || !ctx)
        return false;

    if (type == VssProtocol::PacketType::Meta)
    {
        if (ctx->videoStarted)
            return false;

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return false;

        const QJsonObject meta = doc.object();
        ctx->sensorName = meta["sensorName"].toString();
        ctx->camId = meta["camId"].toString();
        ctx->requestId = meta["requestId"].toString();
        ctx->receivedBytes = 0;
        ctx->startTime = QDateTime();
        ctx->endTime = QDateTime();

        if (ctx->sensorName.isEmpty() || ctx->camId.isEmpty() ||
            ctx->requestId.isEmpty())
            return false;

        m_resultSockets[ctx->requestId] = socket;
        flushPendingResult(socket, ctx->requestId);

        Writter::info(QString("Receive metadata: sensor=%1 cam=%2 requestId=%3")
                      .arg(ctx->sensorName, ctx->camId, ctx->requestId));
        return true;
    }

    if (type == VssProtocol::PacketType::Resume)
    {
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        const QString requestId = obj["requestId"].toString();
        if (requestId.isEmpty())
            return false;

        m_resultSockets[requestId] = socket;
        flushPendingResult(socket, requestId);
        return true;
    }

    if (type == VssProtocol::PacketType::VideoChunk)
    {
        if (ctx->requestId.isEmpty())
            return false;

        if (!ctx->startTime.isValid())
            ctx->startTime = QDateTime::currentDateTime();

        if (!ensureFfmpegRunning(socket, ctx))
            return false;

        return writeVideoChunk(ctx, payload);
    }

    if (type == VssProtocol::PacketType::VideoEnd)
    {
        finishVideo(socket, ctx);
        return true;
    }

    return false;
}

bool VideoServer::writeVideoChunk(ClientContext* ctx,
                                  const QByteArray& payload)
{
    if (!ctx || !ctx->videoStarted || payload.isEmpty())
        return false;

    ctx->receivedBytes += static_cast<quint64>(payload.size());
    qint64 totalWritten = 0;

    while (totalWritten < payload.size())
    {
        const qint64 written = ffmpeg->write(
            payload.constData() + totalWritten,
            payload.size() - totalWritten);

        if (written < 0)
            return false;

        if (written == 0)
        {
            if (!ffmpeg->waitForBytesWritten(30000))
                return false;
            continue;
        }

        totalWritten += written;

        while (ffmpeg->bytesToWrite() > 4 * 1024 * 1024)
        {
            if (!ffmpeg->waitForBytesWritten(30000))
                return false;
        }
    }

    return ffmpeg->state() == QProcess::Running;
}

void VideoServer::finishVideo(QTcpSocket* socket, ClientContext* ctx)
{
    if (!socket || !ctx || ctx->requestId.isEmpty())
        return;

    const QString requestId = ctx->requestId;

    if (!ctx->videoStarted)
    {
        QJsonObject result;
        result["requestId"] = requestId;
        result["success"] = false;
        result["error"] = "VideoEnd received before video data";
        sendOrQueueResult(requestId,
                          socket,
                          VssProtocol::PacketType::Error,
                          result);
        ctx->requestId.clear();
        return;
    }

    stopFfmpeg();
    ctx->videoStarted = false;
    ctx->endTime = QDateTime::currentDateTime();

    if (ffmpeg->exitStatus() != QProcess::NormalExit ||
        ffmpeg->exitCode() != 0)
    {
        QJsonObject result;
        result["requestId"] = requestId;
        result["success"] = false;
        result["error"] = QString("Server FFmpeg failed: %1")
                .arg(QString::fromUtf8(ffmpeg->readAllStandardError()));
        sendOrQueueResult(requestId,
                          socket,
                          VssProtocol::PacketType::Error,
                          result);

        ctx->requestId.clear();
        ctx->sensorName.clear();
        ctx->camId.clear();
        ctx->savePath.clear();
        ctx->receivedBytes = 0;
        return;
    }

    const qint64 elapsedMs = ctx->startTime.isValid()
            ? ctx->startTime.msecsTo(ctx->endTime) : 0;
    const QString videoPath = ctx->savePath;
    const QFileInfo fileInfo(videoPath);

    if (fileInfo.exists() && fileInfo.size() > 0)
    {
        Writter::info(QString("Saved Video: %1 size=%2 elapsed=%3 ms")
                      .arg(videoPath)
                      .arg(fileInfo.size())
                      .arg(elapsedMs));

        m_requestIdsByVideoPath[videoPath] = requestId;
        m_resultSockets[requestId] = socket;
        emit requestSummarize(videoPath);
    }
    else
    {
        QJsonObject result;
        result["requestId"] = requestId;
        result["success"] = false;
        result["error"] = QString("Saved file is missing or empty: %1")
                .arg(videoPath);
        sendOrQueueResult(requestId,
                          socket,
                          VssProtocol::PacketType::Error,
                          result);
    }

    ctx->requestId.clear();
    ctx->sensorName.clear();
    ctx->camId.clear();
    ctx->savePath.clear();
    ctx->receivedBytes = 0;
}

void VideoServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    QString remainSocketLog = QString("Remaining on disconnect : %1")
                .arg(socket->bytesAvailable());

    QString disconnectLog = QString("Client disconnected: %1 %2 localPort = %3")
                .arg(socket->peerAddress().toString())
                .arg(socket->peerPort())
                .arg(socket->localPort());

    Writter::info(remainSocketLog);
    Writter::info(disconnectLog);

    if (m_clients.contains(socket)) {
        ClientContext *ctx = m_clients.take(socket);
        const QString requestId = ctx->requestId;

        if (ctx->videoStarted)
            stopFfmpeg();

        if (!requestId.isEmpty()) {
            QJsonObject result;
            result["requestId"] = requestId;
            result["success"] = false;
            result["error"] = "Socket disconnected before VideoEnd";
            sendOrQueueResult(requestId,
                              nullptr,
                              VssProtocol::PacketType::Error,
                              result);
        }

        delete ctx;
    }

    socket->deleteLater();
}

