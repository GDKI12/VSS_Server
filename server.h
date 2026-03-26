#ifndef VIDEOSEVER_H
#define VIDEOSEVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QProcess>
#include <QString>
#include "videoHandler.h"

class VideoServer : public QObject
{
    Q_OBJECT

public:
    explicit VideoServer(quint16 port, QObject *parent = nullptr);
    ~VideoServer();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    struct ClientContext {
        QProcess *ffmpeg = nullptr;
        QString savePath;
        quint64 receivedBytes = 0;
    };

    void startFfmpegForClient(QTcpSocket *socket, ClientContext *ctx);

    QTcpServer m_server;
    QHash<QTcpSocket*, ClientContext*> m_clients;
    VideoHandler* handler = nullptr;
    quint16 m_port = 0;
};


#endif // VIDEOSEVER_H
