#ifndef EVENTHANDLER_H
#define EVENTHANDLER_H

#include "vssAPI.h"
#include "Log/vssLog.h"
#include "server.h"
#include "apiserver.h"

class ServerManager : public QObject
{
    Q_OBJECT
public:
    explicit ServerManager(QObject* parent = nullptr);
    ~ServerManager();
    void terminate();
    void videoTestInit();
    void testStart();

private:
    void sendJson(QTcpSocket* socket, int statusCode, const QJsonObject& json);
public slots:
    void saveTestLog(const QString& videoPath, const QString& answer, int inferTime);
#ifdef TEST
    void test(const QString&);
#endif
signals:
    void requestToAddLog(QMap<QString, QString>);
    void finishedSendToClient();

private:
    APIServer apiServer;
    std::shared_ptr<VideoServer> server1;
    std::shared_ptr<VideoServer> server2;
    std::shared_ptr<VideoServer> server3;

    VSSLog* logger = nullptr;
    VssAPI* apiManager = nullptr;

    ClipInfo clipInfos;

    //Test시 사용
    QQueue<QString> testVideoQue;

    QMutex mutex;


#ifdef TEST
    QQueue<TestData> testList;
#endif
};

#endif // EVENTHANDLER_H
