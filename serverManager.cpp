#include "serverManager.h"


ServerManager::ServerManager(QObject* parent) : QObject(parent), testPort(4303)
{
    qRegisterMetaType<LogLevel>("LogLevel");

    connect(&testServer, &QTcpServer::newConnection, this, &ServerManager::onNewConnection);

    if(!testServer.listen(QHostAddress::Any, testPort))
        Writter::error("Connected to test server is failed");
    else
        Writter::info("Success to connect to Test server");



    apiManager = new VssAPI();
    logger = new VSSLog(apiManager->getLogPath());

    server1 = std::make_shared<VideoServer>("cam1", 5000);
    server2 = std::make_shared<VideoServer>("cam2", 5001);
    server3 = std::make_shared<VideoServer>("cam3", 5002);

    // TODO
    // How many server to load
    camSize = 3;

    connect(server1.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);
    connect(server2.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);
    connect(server3.get(), &VideoServer::requestEnqueue, apiManager, &VssAPI::enqueueUpload);

    connect(server1.get(), &VideoServer::requestLog, this, &ServerManager::getReplies);
    connect(server2.get(), &VideoServer::requestLog, this, &ServerManager::getReplies);
    connect(server3.get(), &VideoServer::requestLog, this, &ServerManager::getReplies);

    connect(apiManager, &VssAPI::requestToSend, this, &ServerManager::getReplies);
    connect(this, &ServerManager::requestToAddLog, logger, &VSSLog::addLog);
}

ServerManager::~ServerManager()
{
    if(testSocket && testSocket->state() == QAbstractSocket::ConnectedState)
        testSocket->disconnectFromHost();
    if(testSocket)
        testSocket->deleteLater();
}

void ServerManager::onNewConnection()
{
    while(testServer.hasPendingConnections())
    {
        QTcpSocket* socket = testServer.nextPendingConnection();
        testSocket = socket;

        Writter::info("Test client connected");

        connect(testSocket, &QTcpSocket::disconnected, this, [this, socket](){
            if(testSocket == socket)
                testSocket = nullptr;

            socket->deleteLater();
            Writter::info("Test client disconnected");
        });
    }
}

void ServerManager::sendToClient(const QString& result)
{
    Writter::info(result);
    QJsonObject obj;

    obj["result"] = true;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    data.append("\n");

    if(testSocket && testSocket->state() == QAbstractSocket::ConnectedState)
    {
        testSocket->write(data);
        testSocket->flush();
        Writter::info(QString("Send result to test client : %1").arg(QString::fromUtf8(data)));
    }else
    {
        Writter::info("Test client is not connected");
    }
}

void ServerManager::getReplies(const QString& videoPath, const QString& text)
{
    ClipInfo clip;

    QMap<QString, QString> result;

    QString currSection;
    QStringList lines = text.split('\n');


    for(auto itr = lines.begin(); itr != lines.end(); itr++)
    {
        if(*itr == "[Weather]")
        {
            if(itr+1 == lines.end())
                result["Weather"] = "";
            else
                result["Weather"] = *(itr+1);

        }
        else if(*itr == "[Event]")
        {
            if(itr+1 == lines.end())
                result["Event"] = "";
            else
                result["Event"] = *(itr+1);
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

    clip.event = result["Event"];
    clip.weather = result["Weather"];
    clip.videoPath = videoPath;

    emit requestToAddLog(clip);

    taskPool.push_back(clip);
    Writter::info(QString("Current pool size is %1").arg(taskPool.size()));
    if(taskPool.size() == 1)
    {
        Writter::info("Request to client~");
        sendToClient("test");
        taskPool.clear();

    }
}
