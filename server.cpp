#include "server.h"

VideoServer::VideoServer(QString name, quint16 port, QObject *parent)
    : QObject(parent), m_port(port), name(name), ctn(0)
{

    uploadThread = new QThread(this);
    handler = new VideoHandler();
    handler->moveToThread(uploadThread);

    QString logPath = handler->getLogPath();
    logger = new VSSLog(logPath);

    // log event
    connect(this, &VideoServer::requestLog, logger, &VSSLog::addLog);

    // upload thread delete event
    connect(uploadThread, &QThread::finished,
            handler, &QObject::deleteLater);

    // upload event
    connect(handler, &VideoHandler::uploadFinished,
            this, [this](const QString &videoPath, const QString &videoId) {
                emit handler->outInfo(QString("Success to upload, videoPath = %1, videoId = %2").arg(videoPath).arg(videoId));
            });

    // upload fail event
    connect(handler, &VideoHandler::uploadFailed,
            this, [this](const QString &videoPath, const QString &reason) {
                emit handler->requestToSend(videoPath, "");
                emit handler->outWarn(QString("Fail to upload video path = %1 reason = %2")
                                      .arg(videoPath).arg(reason));
            });

    // sent to client event
    connect(handler, &VideoHandler::requestToSend, this, &VideoServer::sendToClient);

    uploadThread->start();
    QMetaObject::invokeMethod(handler, "initialize", Qt::QueuedConnection);

    // video socket new connection event
    connect(&m_server, &QTcpServer::newConnection,
            this, &VideoServer::onNewConnection);

    // meta info socket new connection event
    connect(&m_metaServer, &QTcpServer::newConnection, this, &VideoServer::onMetaConnection);

    if (!m_server.listen(QHostAddress::Any, port))
        emit handler->outError("Stream server listen failed.");
    else
        emit handler->outInfo(QString("Stream server %1 listening on port %2").arg(name).arg(port));

    if(!m_metaServer.listen(QHostAddress::Any, port + 100))
        emit handler->outError("meta info server listen failed.");
    else
        emit handler->outInfo(QString("Meta server   %1 listening on port %2")
                         .arg(name).arg(port+100));


    QDir().mkpath("/home/cscho/vss");
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
    emit requestLog(dirName, answer);

    obj["fileName"] = dirName;
    obj["answer"] = answer;

    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    data.append("\n");

    if (metaSocket && metaSocket->state() == QAbstractSocket::ConnectedState)
    {
        metaSocket->write(data);
        metaSocket->flush();
    }

}
VideoServer::~VideoServer()
{
    emit handler->outInfo("Terminating");
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket *socket = it.key();
        ClientContext *ctx = it.value();

        if (ctx->ffmpeg) {
            if (ctx->ffmpeg->state() == QProcess::Running) {
                ctx->ffmpeg->closeWriteChannel();
                ctx->ffmpeg->waitForFinished(60000);
            }
            delete ctx->ffmpeg;
        }

        delete ctx;
        if (socket) {
            socket->deleteLater();
        }
    }

    m_clients.clear();

    if(uploadThread)
    {
        handler->deleteLater();
        uploadThread->quit();
        uploadThread->wait();

        uploadThread = nullptr;
    }

    handler = nullptr;
}

void VideoServer::startFfmpegForClient(QTcpSocket *socket, ClientContext *ctx)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");

    QString peerIp = socket->peerAddress().toString();

    QString fileName;

    if(m_metaByIp.contains(peerIp) && !m_metaByIp[peerIp].isEmpty())
    {
        QJsonObject meta = m_metaByIp[peerIp].dequeue();

        fileName = meta["videoName"].toString();
    }

    peerIp.replace(":", "_");

    QString dirPath = QString("/home/cscho/vss/%1").arg(fileName);

    QDir().mkdir("/home/cscho/vss");

    ctx->savePath = dirPath;

    ctx->ffmpeg = new QProcess(this);
    ctx->ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);


    connect(ctx->ffmpeg,
            static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status)
            {
                emit handler->outInfo(QString("Finish ffmpeg exitCode = %1 status = %2").arg(exitCode).arg(status));
            });

    QStringList args;
    args << "-hide_banner"
         << "-loglevel" << "info"
         << "-y"
         << "-f" << "mpegts"
         << "-fflags" << "+genpts"
         << "-i" << "pipe:0"
         << "-c:v" << "copy"
         << "-movflags" << "+faststart"
         << ctx->savePath;

    ctx->ffmpeg->setProgram("ffmpeg");
    ctx->ffmpeg->setArguments(args);
    ctx->ffmpeg->start(QIODevice::ReadWrite);

    if(!ctx->ffmpeg->waitForStarted(3000))
    {
        emit handler->outError("Fail to start ffmpeg");
    }

}

void VideoServer::onNewConnection()
{
    while (m_server.hasPendingConnections())
    {
        QTcpSocket *socket = m_server.nextPendingConnection();

        auto *ctx = new ClientContext;
        ctx->ffmpeg = nullptr;
        m_clients.insert(socket, ctx);

        connect(socket, &QTcpSocket::readyRead, this, &VideoServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &VideoServer::onDisconnected);


        emit handler->outInfo(QString("Client connected: %1 %2 localPort = %3")
                              .arg(socket->peerAddress().toString())
                              .arg(socket->peerPort())
                              .arg(socket->localPort()));
    }
}

void VideoServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if (!socket || !m_clients.contains(socket))
        return;

    ClientContext *ctx = m_clients.value(socket);
    if (!ctx)
        return;

    QByteArray data = socket->readAll();
    if (data.isEmpty())
        return;

    if (!ctx->startTime.isValid()) {
        ctx->startTime = QDateTime::currentDateTime();
    }

    // 첫 데이터 수신 시점에 ffmpeg 시작
    if (!ctx->ffmpeg) {
        startFfmpegForClient(socket, ctx);

        if (!ctx->ffmpeg || ctx->ffmpeg->state() != QProcess::Running) {

            emit handler->outWarn(QString("ffmpeg is not running for client %1")
                                  .arg(socket->peerAddress().toString()));
            return;
        }
    }

    ctx->receivedBytes += static_cast<quint64>(data.size());

    qint64 totalWritten = 0;
    while (totalWritten < data.size())
    {
        qint64 n = ctx->ffmpeg->write(data.constData() + totalWritten,
                                      data.size() - totalWritten);
        if (n < 0) {
            emit handler->outWarn(QString("Failed to write stream to ffmpeg: %1")
                                  .arg(ctx->ffmpeg->errorString()));
            return;
        }

        if (n == 0) {
            if (!ctx->ffmpeg->waitForBytesWritten(60000)) {
                emit handler->outWarn(QString("ffmpeg stdin flush timeout: %1")
                                      .arg(ctx->ffmpeg->errorString()));
                return;
            }
            continue;
        }

        totalWritten += n;
    }
}

void VideoServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    emit handler->outInfo(QString("Remaining on disconnect : %1")
                          .arg(socket->bytesAvailable()));


    emit handler->outInfo(QString("Client disconnected: %1 %2 localPort = %3")
                          .arg(socket->peerAddress().toString())
                          .arg(socket->peerPort())
                          .arg(socket->localPort()));

    if (m_clients.contains(socket)) {
        ClientContext *ctx = m_clients.take(socket);
        ctx->endTime = QDateTime::currentDateTime();

        qint64 elapsedMs = ctx->startTime.isValid()
                         ? ctx->startTime.msecsTo(ctx->endTime)
                         : 0;

        encodeTimes.push_back(elapsedMs);
        ctn++;

        if(ctn == 10)
        {
            if(!encodeTimes.isEmpty())
            {
                auto result = std::minmax_element(encodeTimes.cbegin(), encodeTimes.cend());

                qint64 minValue = *result.first;
                qint64 maxValue = *result.second;

                qint64 sum = 0;

                for(auto itr = encodeTimes.cbegin(); itr != encodeTimes.cend(); itr++)
                    sum += *itr;

                qint64 avg =  sum / encodeTimes.size();

                emit handler->outInfo(QString("Encoding ( Max : %1, Min : %2, Avg : %3").arg(maxValue).arg(minValue).arg(avg));
            }

            ctn = 0;
            encodeTimes.clear();
        }

        if (ctx->ffmpeg) {
            if (ctx->ffmpeg->state() == QProcess::Running) {
                ctx->ffmpeg->closeWriteChannel();

                if (!ctx->ffmpeg->waitForFinished(60000)) {
                    emit handler->outWarn(QString("ffmpeg did not finish cleanly for %1").arg(ctx->savePath));
                    ctx->ffmpeg->kill();
                    ctx->ffmpeg->waitForFinished(60000);
                }
            }

            QFileInfo fi(ctx->savePath);
            if (fi.exists() && fi.size() > 0) {

                emit handler->outInfo(QString("Saved Video : %1 size = %2 bytes elapsed = %3 ms")
                                      .arg(ctx->savePath)
                                      .arg(fi.size())
                                      .arg(elapsedMs));

                const QString savePath = ctx->savePath;
                QMetaObject::invokeMethod(
                            handler,
                            [this, savePath](){
                    handler->enqueueUpload(savePath);
                }, Qt::QueuedConnection);
            } else {
                emit handler->outWarn(QString("Saved file is  missing or empty : %1").arg(ctx->savePath));
            }

            delete ctx->ffmpeg;
            ctx->ffmpeg = nullptr;
        }

        delete ctx;
    }

    socket->deleteLater();
}

void VideoServer::onMetaConnection()
{
    while(m_metaServer.hasPendingConnections())
    {
        metaSocket = m_metaServer.nextPendingConnection();

        connect(metaSocket, &QTcpSocket::readyRead, this, &VideoServer::onMetaRead);
        connect(metaSocket, &QTcpSocket::disconnected, metaSocket, &QTcpSocket::deleteLater);
    }
}

void VideoServer::onMetaRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());

    if(!socket)
        return;

    QByteArray data = socket->readAll();
    if(data.isEmpty())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if(!doc.isObject())
        return;

    QJsonObject obj = doc.object();
    QString ip = socket->peerAddress().toString();

    m_metaByIp[ip].enqueue(obj);

    QString videoName = obj["videoName"].toString();

    int idx = videoName.indexOf("_cam");

    QString sensorName;

    if(idx != -1)
        sensorName = videoName.left(idx);


    emit handler->outInfo(QString("Receive Meta data videoName:%1 queue size: %2")
                          .arg(videoName)
                          .arg(m_metaByIp[ip].size()));

}
