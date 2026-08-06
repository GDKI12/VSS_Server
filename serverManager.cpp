#include "serverManager.h"
#include <toml.hpp>

ServerManager::ServerManager(QObject* parent) : QObject(parent), vssPort(4303)
{
    qRegisterMetaType<LogLevel>("LogLevel");

    auto data = toml::parse(CONFIG_FILE.toStdString());
    QString savePath = QString::fromStdString(toml::find<std::string>(data,"setting","video_path"));

    connect(&vssServer, &QTcpServer::newConnection, this, &ServerManager::onNewConnection);

    if(!vssServer.listen(QHostAddress::Any, vssPort))
        Writter::error("Connected to vss server is failed");
    else
        Writter::info("Success to connect to vss server");


    apiManager = new VssAPI();
    logger = new VSSLog(apiManager->getLogPath());

    server1 = std::make_shared<VideoServer>("cam1", 5000);
    server2 = std::make_shared<VideoServer>("cam2", 5001);
    server3 = std::make_shared<VideoServer>("cam3", 5002);

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

#endif

    connect(server1.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);
    connect(server2.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);
    connect(server3.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);

    connect(server1.get(), &VideoServer::requestSummarize, apiManager, &VssAPI::uploadVideo);
    connect(server2.get(), &VideoServer::requestSummarize, apiManager, &VssAPI::uploadVideo);
    connect(server3.get(), &VideoServer::requestSummarize, apiManager, &VssAPI::uploadVideo);

    connect(apiManager, &VssAPI::requestToSend, this, &ServerManager::getReplies);
    connect(this, &ServerManager::finishedSendToClient, apiManager, &VssAPI::startNextUpload);

#ifdef TEST
    connect(apiManager, &VssAPI::onTest, this, &ServerManager::test);
#endif
    connect(this, &ServerManager::requestToAddLog, logger, &VSSLog::addLog);
}

ServerManager::~ServerManager()
{
    if(vssSocket && vssSocket->state() == QAbstractSocket::ConnectedState)
        vssSocket->disconnectFromHost();
    if(vssSocket)
        vssSocket->deleteLater();
}

void ServerManager::terminate()
{
    if(vssSocket && vssSocket->state() == QAbstractSocket::ConnectedState)
        vssSocket->disconnectFromHost();
    if(vssSocket)
        vssSocket->deleteLater();

    logger->deleteLater();
    apiManager->deleteLater();
}
void ServerManager::onNewConnection()
{
    while(vssServer.hasPendingConnections())
    {
        vssSocket = vssServer.nextPendingConnection();

        connect(vssSocket, &QTcpSocket::readyRead, this, &ServerManager::getInitParams);
        connect(vssSocket, &QTcpSocket::disconnected, vssSocket, &QTcpSocket::deleteLater);
    }
}
#ifdef TEST
void ServerManager::test(const QString& path)
{
//    QFileInfo fi(path);
//    QString fileName = fi.baseName();

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
void ServerManager::sendToClient(const VssInfo& result)
{
    Writter::info("Strat to send");


    QJsonObject obj;

    obj["isEvent"] = result.isEvent;
    obj["weather"] = result.weather;
    obj["eventType"] = result.eventType;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    data.append("\n");

    if(vssSocket && vssSocket->state() == QAbstractSocket::ConnectedState)
    {
        vssSocket->write(data);
        vssSocket->flush();
        Writter::info(QString("Send result to test client : %1").arg(QString::fromUtf8(data).remove('\n')));
    }else
    {
        Writter::info("Test client is not connected");
    }
}

void ServerManager::getInitParams()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if(!socket)
    {
        // STOP Program
        Writter::error("Fail to create Init Socket");
        terminate();
        return;
    }
    Writter::info("Start to init params");
    QByteArray buffer;
    buffer.append(socket->readAll());

    Writter::info(QString("Init prams: %1").arg(QString::fromUtf8(buffer)));

    memcpy(&initConfig, buffer.constData(), sizeof(InitConfig));

    Writter::info(QString("Get init parmas camSize : %1, videoLength : %2, fps : %3")
                  .arg(initConfig.camSize).arg(initConfig.videoLength).arg(initConfig.fps));
}

void ServerManager::getReplies(const QString& videoPath, const QString& text, int inferTime)
{
    ClipInfo clip;
    VssInfo vssInfo;
    QMap<QString, QString> result;

    QString currSection;
    QStringList lines = text.split('\n');


    for(auto itr = lines.begin(); itr != lines.end(); itr++)
    {
        QString line = *itr;
        QStringList syntex = line.split(':');
        if(syntex[0] == "Weather")
        {
            result["Weather"] = syntex[1];
        }else if(syntex[0] == "Event")
        {
            result["Event"] = syntex[1];
        }
    }

    QFileInfo fi(videoPath);
    QString fileName = fi.baseName();

    QRegularExpression re("(Sensor_Data_\\d{8})\\d*_(cam\\d+)");
    QRegularExpressionMatch match = re.match(fileName);

    if (match.hasMatch())
    {
        clip.sensorName = match.captured(1);
        clip.camName    = match.captured(2);
    }

    clip.event = result["Event"].trimmed().toLower();
    clip.weather = result["Weather"].trimmed().toLower();
    clip.videoPath = videoPath;

    emit requestToAddLog(clip);


    if(clip.weather.contains("snow"))
        vssInfo.weather = 0x04;
    else if(clip.weather.contains("rain"))
        vssInfo.weather = 0x03;
    else if(clip.weather.contains("fog"))
        vssInfo.weather = 0x02;
    else if(clip.weather.contains("overcast"))
        vssInfo.weather = 0x01;
    else
        vssInfo.weather = 0x00;

    if(clip.event.contains("traffic accident"))
        vssInfo.eventType = 0x03;
    else if(clip.event.contains("road construction"))
        vssInfo.eventType = 0x02;
    else if(clip.event.contains("jaywalking"))
        vssInfo.eventType = 0x01;
    else
        vssInfo.eventType = 0x00;

    if(vssInfo.weather == 0x00 && vssInfo.eventType == 0x00)
        vssInfo.isEvent = 0x00;
    else
        vssInfo.isEvent = 0x01;

    taskPool.push_back(vssInfo);
    inferTimeList.push_back(inferTime);

    Writter::info(QString("Current cam is %1").arg(taskPool.size()));
    Writter::info(QString("Weather: %1, Event: %2").arg(clip.weather, clip.event));

    if(taskPool.size() == initConfig.camSize)
    {
        float totalTime = 0;
        for(int i = 0; i < taskPool.size(); i++)
            totalTime += inferTimeList[i];

        totalTime = totalTime/taskPool.size();

        Writter::info(QString("All Summarize process is Done, Mean Time is %1").arg(totalTime));
        Writter::info("Request sent to client");
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
        inferTimeList.clear();
    }
}
