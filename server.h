#ifndef VIDEOSEVER_H
#define VIDEOSEVER_H

#include "vssAPI.h"
#include "Log/vssLog.h"
#include "vssProtocol.h"
#include <QPointer>


class VideoServer : public QObject
{
    Q_OBJECT

public:
    explicit VideoServer(QString name, quint16 port, QObject *parent = nullptr);
    ~VideoServer();
    void sendToClient(const QString& videoPath, const QString& answer);
    void sendErrorToClient(const QString& videoPath, const QString& error);

private:
    void stopFfmpeg();
    bool ensureFfmpegRunning(QTcpSocket *socket, ClientContext *ctx);
    bool processPacket(QTcpSocket* socket,
                       ClientContext* ctx,
                       VssProtocol::PacketType type,
                       const QByteArray& payload);
    bool writeVideoChunk(ClientContext* ctx, const QByteArray& payload);
    void finishVideo(QTcpSocket* socket, ClientContext* ctx);
    bool sendPacket(QTcpSocket* socket,
                    VssProtocol::PacketType type,
                    const QByteArray& payload);
    void sendOrQueueResult(const QString& requestId,
                           QTcpSocket* socket,
                           VssProtocol::PacketType type,
                           const QJsonObject& result);
    void flushPendingResult(QTcpSocket* socket, const QString& requestId);
    QString takeRequestId(const QString& videoPath);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

signals:
    void requestEnqueue(const QString&);
    void requestSummarize(const QString&);

private:
    QMutex ffmpegMutex;
    QProcess* ffmpeg;
    QTcpServer m_server;
    QHash<QTcpSocket*, ClientContext*> m_clients;

    QString name;

    QHash<QString, QString> m_requestIdsByVideoPath;
    QHash<QString, QPointer<QTcpSocket>> m_resultSockets;
    QHash<QString, QByteArray> m_pendingResults;

    QString savePath;
};


#endif // VIDEOSEVER_H
