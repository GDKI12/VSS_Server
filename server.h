#ifndef VIDEOSEVER_H
#define VIDEOSEVER_H

#include "vssAPI.h"
#include "Log/vssLog.h"


class VideoServer : public QObject
{
    Q_OBJECT

public:
    explicit VideoServer(QString name, quint16 port, QObject *parent = nullptr);
    ~VideoServer();

private:
    void stopFfmpeg();
    bool ensureFfmpegRunning(QTcpSocket *socket, ClientContext *ctx);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void sendToClient(const QString& videoPath, const QString& answer);

    // meata data
    void onMetaConnection();
    void onMetaRead();

signals:
    void requestEnqueue(const QString&);
    void requestLog(const QString& sensorName, const QString& answer, const QString& videoPath);

private:
    QMutex ffmpegMutex;
    QProcess* ffmpeg;
    QTcpServer m_server;
    QHash<QTcpSocket*, ClientContext*> m_clients;

    QQueue<QString> m_uploadQueue;
    bool m_uploading;
    QString currentVideoPath;

    quint16 m_port = 0;
    QString name;

    // meta data
    QTcpServer m_metaServer;
    QHash<QString, QQueue<QJsonObject>> m_metaByIp;

    QTcpSocket* metaSocket;

    QVector<qint64> encodeTimes;

    int ctn;
    QString savePath;
};


#endif // VIDEOSEVER_H
