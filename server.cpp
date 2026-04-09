#include "server.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

VideoServer::VideoServer(QString name, quint16 port, QObject *parent)
    : QObject(parent), m_port(port), name(name)
{
    uploadThread = new QThread(this);
    handler = new VideoHandler();
    handler->moveToThread(uploadThread);

    connect(uploadThread, &QThread::finished,
            handler, &QObject::deleteLater);

    connect(handler, &VideoHandler::uploadFinished,
            this, [](const QString &videoPath, const QString &videoId) {
                qDebug() << "[Server] upload finished:"
                         << "path =" << videoPath
                         << "videoId =" << videoId;
            });

    connect(handler, &VideoHandler::uploadFailed,
            this, [](const QString &videoPath, const QString &reason) {
                qWarning() << "[Server] upload failed:"
                           << "path =" << videoPath
                           << "reason =" << reason;
            });

    uploadThread->start();
    QMetaObject::invokeMethod(handler, "initialize", Qt::QueuedConnection);

    connect(&m_server, &QTcpServer::newConnection,
            this, &VideoServer::onNewConnection);

    if (!m_server.listen(QHostAddress::Any, port)) {
        qFatal("Server listen failed.");
    }

    qDebug() << "Server listening on port" << port;

    QDir().mkpath("/home/cscho/vss");
}

VideoServer::~VideoServer()
{
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket *socket = it.key();
        ClientContext *ctx = it.value();

        if (ctx->ffmpeg) {
            if (ctx->ffmpeg->state() == QProcess::Running) {
                ctx->ffmpeg->closeWriteChannel();
                ctx->ffmpeg->waitForFinished(5000);
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
    peerIp.replace(":", "_");

    ctx->savePath = QString("/home/cscho/vss/%1_%2.mp4")
            .arg(name)
            .arg(timestamp);

    ctx->ffmpeg = new QProcess(this);
    ctx->ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);

//    connect(ctx->ffmpeg, &QProcess::readyReadStandardError, this, [ctx](){
//        const QByteArray err = ctx->ffmpeg->readAllStandardError();
//        if(!err.isEmpty())
//        {
//            qCritical() << "[ffmpeg]" << QString::fromLocal8Bit(err);
//        }
//    });

    connect(ctx->ffmpeg,
            static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            [ctx](int exitCode, QProcess::ExitStatus status)
            {
                qDebug() << "ffmpeg finished:"
                                         << "exitCode =" << exitCode
                                         << "status =" << status
                                         << "savePath =" << ctx->savePath;
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
        qWarning() << "Fail to start ffmpeg for " << ctx->savePath << " " << ctx->ffmpeg->errorString();
    }

}

void VideoServer::onNewConnection()
{
    while (m_server.hasPendingConnections())
    {
        QTcpSocket *socket = m_server.nextPendingConnection();

        auto *ctx = new ClientContext;
        ctx->ffmpeg = nullptr;   // 명시적으로 초기화 권장
        m_clients.insert(socket, ctx);

        connect(socket, &QTcpSocket::readyRead, this, &VideoServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &VideoServer::onDisconnected);

        qDebug() << "Client connected:"
                 << socket->peerAddress().toString()
                 << socket->peerPort()
                 << "localPort =" << socket->localPort();
    }
}

void VideoServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

//    qDebug() << "readyRead entered";
//    qDebug() << "bytesAvailable =" << socket->bytesAvailable();

    if (!socket || !m_clients.contains(socket))
        return;

    ClientContext *ctx = m_clients.value(socket);
    if (!ctx)
        return;

    QByteArray data = socket->readAll();
//    qDebug() << "read bytes =" << data.size();
    if (data.isEmpty())
        return;

    if (!ctx->startTime.isValid()) {
        ctx->startTime = QDateTime::currentDateTime();
    }

    // 첫 데이터 수신 시점에 ffmpeg 시작
    if (!ctx->ffmpeg) {
        startFfmpegForClient(socket, ctx);

        if (!ctx->ffmpeg || ctx->ffmpeg->state() != QProcess::Running) {
            qWarning() << "ffmpeg is not running for client"
                       << socket->peerAddress().toString();
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
            qWarning() << "Failed to write stream to ffmpeg:"
                       << ctx->ffmpeg->errorString();
            return;
        }

        if (n == 0) {
            if (!ctx->ffmpeg->waitForBytesWritten(3000)) {
                qWarning() << "ffmpeg stdin flush timeout:"
                           << ctx->ffmpeg->errorString();
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

    qDebug() << "remaining on disconnect =" << socket->bytesAvailable();

    qDebug() << "Client disconnected:"
             << socket->peerAddress().toString()
             << socket->peerPort()
             << "localPort =" << socket->localPort();

    if (m_clients.contains(socket)) {
        ClientContext *ctx = m_clients.take(socket);
        ctx->endTime = QDateTime::currentDateTime();

        qint64 elapsedMs = ctx->startTime.isValid()
                         ? ctx->startTime.msecsTo(ctx->endTime)
                         : 0;

        if (ctx->ffmpeg) {
            if (ctx->ffmpeg->state() == QProcess::Running) {
                ctx->ffmpeg->closeWriteChannel();

                if (!ctx->ffmpeg->waitForFinished(10000)) {
                    qWarning() << "ffmpeg did not finish cleanly for"
                               << ctx->savePath;
                    ctx->ffmpeg->kill();
                    ctx->ffmpeg->waitForFinished(3000);
                }
            }

            QFileInfo fi(ctx->savePath);
            if (fi.exists() && fi.size() > 0) {
                qDebug() << "[" << name << "]";
                qDebug() << "Saved stream:" << ctx->savePath
                         << "size =" << fi.size() << "bytes"
                         << "elapsed =" << elapsedMs << "ms";

                QMetaObject::invokeMethod(handler,
                                          "enqueueUpload",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, ctx->savePath));
            } else {
                qWarning() << "Saved file missing or empty:" << ctx->savePath;
            }

            delete ctx->ffmpeg;
            ctx->ffmpeg = nullptr;
        }

        delete ctx;
    }

    socket->deleteLater();
}
