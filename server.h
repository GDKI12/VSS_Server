#ifndef VIDEOSEVER_H
#define VIDEOSEVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QProcess>
#include <QThread>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include "videoHandler.h"
#include "Log/vssLog.h"


class VideoServer : public QObject
{
    Q_OBJECT

public:
    explicit VideoServer(QString name, quint16 port, QObject *parent = nullptr);
    ~VideoServer();

private:
    struct ClientContext {
        QProcess *ffmpeg = nullptr;
        QString savePath;
        quint64 receivedBytes = 0;
        QDateTime startTime;
        QDateTime endTime;
    };

    void startFfmpegForClient(QTcpSocket *socket, ClientContext *ctx);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void sendToClient(const QString& videoPath, const QString& answer);

    // meata data
    void onMetaConnection();
    void onMetaRead();

signals:
    void requestLog(const QString& sensorName, const QString& answer);

private:
    QTcpServer m_server;
    QHash<QTcpSocket*, ClientContext*> m_clients;
    VideoHandler* handler = nullptr;
    QThread* uploadThread = nullptr;
    quint16 m_port = 0;
    QString name;

    // meta data
    QTcpServer m_metaServer;
    QHash<QString, QQueue<QJsonObject>> m_metaByIp;

    QTcpSocket* metaSocket;

    VSSLog* logger;
};


#endif // VIDEOSEVER_H
