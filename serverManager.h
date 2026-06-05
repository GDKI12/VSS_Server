#ifndef EVENTHANDLER_H
#define EVENTHANDLER_H

#include "vssAPI.h"
#include "Log/vssLog.h"
#include "server.h"

class ServerManager : public QObject
{
    Q_OBJECT
public:
    explicit ServerManager(QObject* parent = nullptr);
    ~ServerManager();
    void sendToClient(const QString&);

public slots:
    void onNewConnection();
    void getReplies(const QString&, const QString&);

signals:
    void requestToAddLog(const ClipInfo&);

private:
    int camSize;
    std::shared_ptr<VideoServer> server1;
    std::shared_ptr<VideoServer> server2;
    std::shared_ptr<VideoServer> server3;

    VSSLog* logger = nullptr;
    VssAPI* apiManager = nullptr;

    QTcpServer testServer;

    QTcpSocket* testSocket = nullptr;
    int testPort;

    QVector<ClipInfo> taskPool;
};

#endif // EVENTHANDLER_H
