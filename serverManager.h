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
    void sendToClient(const VssInfo&);
    void terminate();
public slots:
    void onNewConnection();
    void getReplies(const QString&, const QString&, int);
    void getInitParams();
#ifdef TEST
    void test(const QString&);
#endif
signals:
    void requestToAddLog(const ClipInfo&);
    void finishedSendToClient();

private:
    InitConfig initConfig;
    std::shared_ptr<VideoServer> server1;
    std::shared_ptr<VideoServer> server2;
    std::shared_ptr<VideoServer> server3;

    VSSLog* logger = nullptr;
    VssAPI* apiManager = nullptr;

    QTcpServer vssServer;

    QTcpSocket* vssSocket = nullptr;
    int vssPort;

    QVector<VssInfo> taskPool;
    QVector<int> inferTimeList;
#ifdef TEST
    QQueue<TestData> testList;
#endif
};

#endif // EVENTHANDLER_H
