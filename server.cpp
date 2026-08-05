#include "server.h"
#include <toml.hpp>

VideoServer::VideoServer(QString name, quint16 port, QObject *parent)
    : QObject(parent), m_uploading(false), m_port(port), name(name)
{

    auto data = toml::parse(CONFIG_FILE.toStdString());
    savePath = QString::fromStdString(toml::find<std::string>(data,"setting","video_path"));

    ffmpeg = new QProcess(this);

    // video socket new connection event
    connect(&m_server, &QTcpServer::newConnection,
            this, &VideoServer::onNewConnection);

    // meta info socket new connection event
    connect(&m_metaServer, &QTcpServer::newConnection, this, &VideoServer::onMetaConnection);

    if (!m_server.listen(QHostAddress::Any, port))
        Writter::error("Stream server listen failed.");
    else
        Writter::info(QString("Stream server %1 listening on port %2").arg(name).arg(port));

    if(!m_metaServer.listen(QHostAddress::Any, port + 100))
        Writter::error("meta info server listen failed.");
    else
        Writter::info(QString("Meta server   %1 listening on port %2")
                    .arg(name).arg(port+100));


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

    if(metaSocket)
        metaSocket->deleteLater();

}

bool VideoServer::ensureFfmpegRunning(QTcpSocket *socket, ClientContext *ctx)
{
    if(ffmpeg->state() == QProcess::Running)
        return true;

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");

    QString peerIp = socket->peerAddress().toString();

    QString fileName;

    if(m_metaByIp.contains(peerIp) && !m_metaByIp[peerIp].isEmpty())
    {
        QJsonObject meta = m_metaByIp[peerIp].dequeue();

        fileName = meta["videoName"].toString();
    }

    peerIp.replace(":", "_");

    QString dirPath = QString("%1/%2").arg(savePath, fileName);

    QDir().mkdir(savePath);

    ctx->savePath = dirPath;

    ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);

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

    ffmpeg->setProgram("ffmpeg");
    ffmpeg->setArguments(args);
    ffmpeg->start(QIODevice::ReadWrite);

    if(!ffmpeg->waitForStarted(-1))
    {
        Writter::warn("Fail to start ffmpeg");
        return false;
    }

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

    QByteArray data = socket->readAll();
    if (data.isEmpty())
        return;

    if (!ctx->startTime.isValid()) {
        ctx->startTime = QDateTime::currentDateTime();
    }

    // 첫 데이터 수신 시점에 ffmpeg 시작
    if (!ensureFfmpegRunning(socket, ctx)) {
            qWarning() << QString("ffmpeg is not running for client %1")
                          .arg(socket->peerAddress().toString());
            return;
    }

    ctx->receivedBytes += static_cast<quint64>(data.size());

    qint64 totalWritten = 0;
    while (totalWritten < data.size())
    {
        qint64 n = ffmpeg->write(data.constData() + totalWritten,
                                      data.size() - totalWritten);
        if (n < 0) {
            qWarning() << QString("Failed to write stream to ffmpeg: %1")
                          .arg(ffmpeg->errorString());
            stopFfmpeg();
            return;
        }

        if (n == 0) {
            if (!ffmpeg->waitForBytesWritten(60000)) {
                qWarning() << QString("ffmpeg stdin flush timeout: %1")
                              .arg(ffmpeg->errorString());
                stopFfmpeg();
                return;
            }
            continue;
        }

        totalWritten += n;
    }

    if(ffmpeg->state() == QProcess::NotRunning)
        return;

    return;
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

                QString encodLog = QString("Encoding ( Max : %1, Min : %2, Avg : %3 )").arg(maxValue).arg(minValue).arg(avg);

                Writter::info(encodLog);
            }

            ctn = 0;
            encodeTimes.clear();
        }

        stopFfmpeg();
        Writter::info("Success to save clip");

        QFileInfo fi(ctx->savePath);
        if (fi.exists() && fi.size() > 0) {

            QString ffmpegLog = QString("Saved Video : %1 size = %2 bytes elapsed = %3 ms")
                                  .arg(ctx->savePath)
                                  .arg(fi.size())
                                  .arg(elapsedMs);

            Writter::info(ffmpegLog);

            const QString savePath = ctx->savePath;
//            emit requestEnqueue(savePath);
            emit requestSummarize(savePath);

        } else {
            Writter::warn(QString("Saved file is  missing or empty : %1").arg(ctx->savePath));
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


    QString metaInfoLog = QString("Receive Meta data videoName:%1 queue size: %2")
                          .arg(videoName)
                          .arg(m_metaByIp[ip].size());

    Writter::info(metaInfoLog);

}

