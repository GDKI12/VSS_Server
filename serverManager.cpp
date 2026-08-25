#include "serverManager.h"
#include <QDataStream>
#include <toml.hpp>

ServerManager::ServerManager(QObject* parent) : QObject(parent), vssPort(4303)
{
    qRegisterMetaType<LogLevel>("LogLevel");

    auto data = toml::parse(CONFIG_FILE.toStdString());
    QString savePath = QString::fromStdString(toml::find<std::string>(data,"setting","video_path"));


    if(!vssServer.listen(QHostAddress::Any, vssPort))
        Writter::error("Connected to vss server is failed");
    else
        Writter::info("Success to connect to vss server");


    apiManager = new VssAPI();
    logger = new VSSLog(apiManager->getLogPath());

    server1 = std::make_shared<VideoServer>("cam1", 5000);
    server2 = std::make_shared<VideoServer>("cam2", 5001);
    server3 = std::make_shared<VideoServer>("cam3", 5002);

    connect(&vssServer, &QTcpServer::newConnection, this, &ServerManager::onNewConnection);
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

    connect(apiManager, &VssAPI::requestToSend, this, &ServerManager::getReplies);
    connect(apiManager, &VssAPI::doneSummarizeTest, this, &ServerManager::saveTestLog);
    connect(this, &ServerManager::finishedSendToClient, apiManager, &VssAPI::startNextUpload);

    connect(this, &ServerManager::requestToAddLog, logger, &VSSLog::addLog);
    connect(apiManager, &VssAPI::vssStatus, this, &ServerManager::responseTo);

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
void ServerManager::onNewConnection()
{
    while(vssServer.hasPendingConnections())
    {
        QTcpSocket* socket = vssServer.nextPendingConnection();

        connect(socket, &QTcpSocket::readyRead,
                this, &ServerManager::getInitParams);
        connect(socket, &QTcpSocket::disconnected,
                socket, &QTcpSocket::deleteLater);
    }
}

bool ServerManager::sendToConnectedClients(const QByteArray& data)
{
    bool sent = false;
    const auto sockets = vssServer.findChildren<QTcpSocket*>(
                QString(), Qt::FindDirectChildrenOnly);

    for (QTcpSocket* socket : sockets)
    {
        if (!socket ||
            socket->state() != QAbstractSocket::ConnectedState)
            continue;

        if (socket->write(data) >= 0)
        {
            socket->flush();
            sent = true;
        }
    }

    return sent;
}

void ServerManager::processRequest(QTcpSocket *socket)
{
    QByteArray request = socket->readAll();

    qDebug() << "Request:";
    qDebug().noquote() << request;

    if(request.startsWith("GET /health"))
    {
        apiManager->requestHealth([socket](bool result){
            QJsonObject response;

            response["status"] = result ? "ok" : "error";
            response["isReady"] = true;

            QByteArray body =
                    QJsonDocument(response).toJson(QJsonDocument::Compact);

            socket->write(body);
        });

        return;
    }


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
void ServerManager::sendToClient(ClipInfo clipInfo)
{
    InitConfig test = initConfig;
    ClipInfo testClipInfo = clipInfo;

    Writter::info("Strat to send");

    bool result = false;

    QJsonArray weatherArr;
    QJsonArray timeArr;
    QJsonArray roadArr;
    QJsonArray eventArr;

    for(const QString& str : initConfig.weather)
    {
        if(clipInfo.weather.contains(str))
        {
            weatherArr.append(str);
        }
    }

    for(const QString& str : initConfig.time)
    {
        if(clipInfo.timeOfDay.contains(str))
        {
            timeArr.append(str);
        }
    }

    for(const QString& str : initConfig.roadEnv)
    {
        if(clipInfo.roadType.contains(str))
        {
            roadArr.append(str);
        }
    }

    for(const QString& str : initConfig.scenario)
    {
        if(clipInfo.event.contains(str))
        {
            eventArr.append(str);
        }
    }

    if(!weatherArr.empty() && !timeArr.empty() && !roadArr.empty() && !eventArr.empty())
        result = true;

    QJsonObject obj;

    obj["isSave"] = result;
    obj["sensorName"] = clipInfo.sensorName;
    obj["weatherList"] = weatherArr;
    obj["timeList"] = timeArr;
    obj["roadList"] = roadArr;
    obj["eventList"] = eventArr;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    data.append("\n");

    qDebug() << "object";
    qDebug() << data;
    if(sendToConnectedClients(data))
    {
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
    Writter::info("Get mission");

    paramBuffer.append(socket->readAll());

    constexpr int HEADER_SIZE = sizeof(quint32);


    while (true) {
        if(paramBuffer.size() < HEADER_SIZE)
             return;

         QDataStream headerStream(paramBuffer);
         headerStream.setByteOrder(QDataStream::BigEndian);

         quint32 dataSize = 0;
         headerStream >> dataSize;

         if(paramBuffer.size() < HEADER_SIZE + static_cast<int>(dataSize))
             return;

         const QByteArray initData =
             paramBuffer.mid(HEADER_SIZE, dataSize);

         paramBuffer.remove(
             0,
             HEADER_SIZE + static_cast<int>(dataSize)
         );

         QDataStream dataStream(initData);
         dataStream.setByteOrder(QDataStream::BigEndian);

         dataStream >> this->initConfig.channel;
         dataStream >> this->initConfig.fps;
         dataStream >> this->initConfig.clipLengthSec;
         dataStream >> this->initConfig.targetScenes;
         dataStream >> this->initConfig.deviceType;
         dataStream >> this->initConfig.weather;
         dataStream >> this->initConfig.time;
         dataStream >> this->initConfig.roadEnv;
         dataStream >> this->initConfig.scenario;

         for(QString& str : initConfig.weather)
             str = str.toLower();

         for(QString& str : initConfig.time)
             str = str.toLower();

         for(QString& str : initConfig.roadEnv)
             str = str.toLower();

         for(QString& str : initConfig.scenario)
             str = str.toLower();

         if(dataStream.status() != QDataStream::Ok)
         {
             Writter::error("Init parameter decode failed");
             continue;
         }

        InitConfig test = initConfig;

        Writter::info("Get Mission data");
        return;

    }
}

void ServerManager::responseTo(bool status)
{
    qDebug() << "Start to response";
    qDebug() << "Status is " << status;
    QJsonObject obj;
    obj["isReady"] = status;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    data.append("\n");

    sendToConnectedClients(data);
}
void ServerManager::getReplies(const QString& videoPath, const QString& text, int inferTime)
{

    qDebug() << "VSS Result: " << text;
    QMap<QString, QString> result;

    QStringList lines = text.split('\n');


    for(auto itr = lines.begin(); itr != lines.end(); itr++)
    {
        QString line = *itr;
        if(!line.contains(":"))
            continue;

        QStringList syntex = line.split(':');
        result[syntex[0].trimmed()] = syntex[1].trimmed().toLower();
    }

    QFileInfo fi(videoPath);
    QString fileName = fi.baseName();

    QRegularExpression re("(Sensor_Data_\\d{8})\\d*_(cam\\d+)");
    QRegularExpressionMatch match = re.match(fileName);

    if (match.hasMatch())
    {
        result["sensorName"] = match.captured(1);
        result["camId"]    = match.captured(2);
    }

    result["videoPath"] = videoPath;

    // 로그 추가
    Writter::info("Request to add log");
    emit requestToAddLog(result);

    if(clipInfos.sensorName.isEmpty())
    {
        clipInfos.sensorName = result["sensorName"];
    }

    clipInfos.camId = result["camId"];

    if(result.contains("Weather"))
    {
        if(result["Weather"].contains("clear"))
            clipInfos.weather.append("clear");
        else if(result["Weather"].contains("overcast"))
            clipInfos.weather.append("overcast");
        else if(result["Weather"].contains("fog"))
            clipInfos.weather.append("fog");
        else if(result["Weather"].contains("rain"))
            clipInfos.weather.append("rain");
        else if(result["Weather"].contains("snow"))
            clipInfos.weather.append("snow");
        else
            clipInfos.weather.append("unknown");
    }

    if(result.contains("Time"))
    {
        if(result["Time"].contains("daytime"))
            clipInfos.timeOfDay.append("daytime");
        else if(result["Time"].contains("nighttime"))
            clipInfos.timeOfDay.append("nighttime");
        else
            clipInfos.timeOfDay.append("unknown");
    }

    if(result.contains("Road"))
    {
        if(result["Road"].contains("hightway"))
            clipInfos.roadType.append("hightway");
        else if(result["Road"].contains("urban_arterial"))
            clipInfos.roadType.append("urban_arterial");
        else if(result["Road"].contains("urban_local"))
            clipInfos.roadType.append("urban_local");
        else if(result["Road"].contains("parking_area"))
            clipInfos.roadType.append("parking_area");
        else if(result["Road"].contains("unpaved"))
            clipInfos.roadType.append("unpaved");
        else
            clipInfos.roadType.append("unknown");

    }

    if(result.contains("Event"))
    {
        if(result["Event"].contains("lane_keep"))
            clipInfos.event.append("lane_keep");
        else if(result["Event"].contains("lane_change_merge"))
            clipInfos.event.append("overtake");
        else if(result["Event"].contains("inter_sig"))
            clipInfos.event.append("inter_sig");
        else if(result["Event"].contains("inter_unsig"))
            clipInfos.event.append("inter_unsig");
        else if(result["Event"].contains("inter_round"))
            clipInfos.event.append("inter_round");
        else if(result["Event"].contains("ped_cross"))
            clipInfos.event.append("ped_cross");
        else if(result["Event"].contains("ped_illegal"))
            clipInfos.event.append("ped_illegal");
        else if(result["Event"].contains("construction"))
            clipInfos.event.append("construction");
        else
            clipInfos.event.append("unknown");
    }

    QString log = QString("Request send to client : %1").arg(result["camId"]);
    sendToClient(clipInfos);
}

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
