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
    void sendToClient(ClipInfo clipInfo);
    void terminate();
    void videoTestInit();
    void testStart();

private:
    void processRequest(QTcpSocket* socket);
    void sendJson(QTcpSocket* socket, int statusCode, const QJsonObject& json);
    bool sendToConnectedClients(const QByteArray& data);
public slots:
    void onNewConnection();
    void getReplies(const QString&, const QString&, int);
    void getInitParams();
    void responseTo(bool status);
    void saveTestLog(const QString& videoPath, const QString& answer, int inferTime);
#ifdef TEST
    void test(const QString&);
#endif
signals:
    void requestToAddLog(QMap<QString, QString>);
    void finishedSendToClient();

private:
    std::shared_ptr<VideoServer> server1;
    std::shared_ptr<VideoServer> server2;
    std::shared_ptr<VideoServer> server3;

    VSSLog* logger = nullptr;
    VssAPI* apiManager = nullptr;

    // 미션 설정값
    QByteArray paramBuffer;
    InitConfig initConfig;

    QTcpServer vssServer;

    int vssPort;

    ClipInfo clipInfos;

    //Test시 사용
    QQueue<QString> testVideoQue;

    QMutex mutex;


#ifdef TEST
    QQueue<TestData> testList;
#endif
};

#endif // EVENTHANDLER_H
