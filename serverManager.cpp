#include "serverManager.h"
#include <toml.hpp>

ServerManager::ServerManager(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<LogLevel>("LogLevel");

    auto data = toml::parse(CONFIG_FILE.toStdString());
    QString savePath = QString::fromStdString(toml::find<std::string>(data,"setting","video_path"));

    apiManager = new VssAPI();
    logger = new VSSLog(apiManager->getLogPath());

    if(!apiServer.listen(QHostAddress::Any, 8080))
    {
        Writter::error("Fail to open vss api server");
    }else{
        Writter::info("Success to open vss api server");
    }

    apiManager->requestHealth([this](bool status){
        Writter::info("Check vss agent alive?");
        apiServer.vssHealthyCheck(status);
    });

    server1 = std::make_shared<VideoServer>("cam1", 5000);
    server2 = std::make_shared<VideoServer>("cam2", 5001);
    server3 = std::make_shared<VideoServer>("cam3", 5002);

    connect(server1.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);
    connect(server2.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);
    connect(server3.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);

    const auto connectSummarize = [this](VideoServer* videoServer) {
        QPointer<VideoServer> safeServer(videoServer);

        connect(videoServer, &VideoServer::requestSummarize,
                this,
                [this, safeServer](const QString& videoPath) {
            apiManager->uploadVideo(
                videoPath,
                [safeServer, videoPath](bool success,
                                        const QString& answer,
                                        int inferTime,
                                        const QString& error) {
                if (!safeServer)
                    return;

                if (!success) {
                    Writter::error(QString("Summarize failed for %1: %2")
                                   .arg(videoPath, error));
                    safeServer->sendErrorToClient(videoPath, error);
                    return;
                }

                Writter::info(QString("Summarize callback for %1 (%2 sec)")
                              .arg(videoPath)
                              .arg(inferTime));
                safeServer->sendToClient(videoPath, answer);
            });
        });
    };

    connectSummarize(server1.get());
    connectSummarize(server2.get());
    connectSummarize(server3.get());

    connect(apiManager, &VssAPI::doneSummarizeTest, this, &ServerManager::saveTestLog);
    connect(this, &ServerManager::finishedSendToClient, apiManager, &VssAPI::startNextUpload);

    connect(this, &ServerManager::requestToAddLog, logger, &VSSLog::addLog);

//    videoTestInit();

#ifdef TEST
    auto testData = toml::parse(CONFIG_FILE.toStdString());
    QString testFilePath = QString::fromStdString(toml::find<std::string>(testData,"setting","test_data"));

    QFile fi(testFilePath);
    if(!fi.open(QIODevice::ReadOnly))
        qCritical() << "Fail to open file " << testFilePath;

    QByteArray d = fi.readAll();

    fi.close();

    QJsonDocument doc = QJsonDocument::fromJson(d);

    QJsonArray rootArray = doc.array();

    for(const QJsonValue& value : rootArray)
    {
        QJsonObject obj = value.toObject();

        TestData td;
        td.event = obj.value("event").toString();
        td.weather = obj.value("weather").toString();
        testList.enqueue(td);
    }

    Writter::info(QString("Gathered test data, data size: %1").arg(testList.size()));

    connect(apiManager, &VssAPI::onTest, this, &ServerManager::test);
#endif
}

ServerManager::~ServerManager()
{
    // 처리 필요
}

void ServerManager::saveTestLog(const QString& videoPath, const QString& answer, int inferTime)
{
    qDebug() << "Start to saveTestLog";
    qDebug() << "Video list size" << testVideoQue.size();
    QFile file("/home/cscho/vss_log/test.json");
    QStringList lines = answer.split('\n');

    QMap<QString, QString> result;

    for(auto itr = lines.constBegin(); itr != lines.constEnd(); itr++)
    {
        QString line = *itr;
        if(!line.contains(":"))
            continue;

        QStringList syntex = line.split(':');
        result[syntex[0]] = syntex[1].trimmed();
    }
    result["videoPath"] = videoPath;

    QJsonObject newObj;
    for(auto itr = result.constBegin(); itr != result.constEnd(); itr++)
    {
        if(itr.key() == "sensorName")
            continue;

        newObj[itr.key()] = itr.value();
    }

    if(!file.open(QIODevice::WriteOnly | QIODevice::Append)){
        return;
    }

    file.close();

    if(!file.open(QIODevice::ReadOnly))
        return;

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray rootArr = doc.array();

    rootArr.append(newObj);

    QJsonDocument newDoc = QJsonDocument(rootArr);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(newDoc.toJson(QJsonDocument::Indented));
    file.close();

    if(testVideoQue.isEmpty())
    {
        qDebug() << "End to Test";
        return;
    }
    testStart();
}

void ServerManager::terminate()
{
    logger->deleteLater();
    apiManager->deleteLater();
}


#ifdef TEST
void ServerManager::test(const QString& path)
{
    VssInfo vssInfo;

    TestData td = testList.dequeue();
    QString weather = td.weather.trimmed().toLower();
    QString event = td.event.trimmed().toLower();


    if(weather.contains("snow"))
        vssInfo.weather = 0x04;
    else if(weather.contains("rain"))
        vssInfo.weather = 0x03;
    else if(weather.contains("fog"))
        vssInfo.weather = 0x02;
    else if(weather.contains("overcast"))
        vssInfo.weather = 0x01;
    else
        vssInfo.weather = 0x00;

    if(event.contains("traffic accident"))
        vssInfo.eventType = 0x03;
    else if(event.contains("road construction"))
        vssInfo.eventType = 0x02;
    else if(event.contains("jaywalking"))
        vssInfo.eventType = 0x01;
    else
        vssInfo.eventType = 0x00;

    taskPool.push_back(vssInfo);

    if(taskPool.size() == initConfig.camSize)
    {
        Writter::info("Request to client~");
        // TODO
        VssInfo totalInfo;
        totalInfo.weather = 0x00;
        totalInfo.eventType = 0x00;

        for(int i = 0; i < taskPool.size() ; i++)
        {
            if(taskPool[i].weather >= totalInfo.weather)
                totalInfo.weather = taskPool[i].weather;

            if(taskPool[i].eventType >= totalInfo.eventType)
                totalInfo.eventType = taskPool[i].eventType;
        }

        if(totalInfo.weather == 0x00 && totalInfo.eventType == 0x00)
            totalInfo.isEvent = 0x00;
        else
            totalInfo.isEvent = 0x01;

        sendToClient(totalInfo);
        taskPool.clear();
    }
    emit finishedSendToClient();
}
#endif


void ServerManager::videoTestInit()
{
    QString videoRootPath = "/data/cscho/clip";
    QDir dir(videoRootPath);
    QFileInfoList fiList = dir.entryInfoList({"*mp4"}, QDir::Files, QDir::Name);

    for(QFileInfo fi : fiList)
    {
        testVideoQue.enqueue(fi.absoluteFilePath());
    }

    testStart();
}

void ServerManager::testStart()
{
    QString videoPath = testVideoQue.dequeue();
    apiManager->uploadVideo(videoPath);
}
