#include "vssLog.h"

VSSLog::VSSLog(const QString& rootPath, QObject* parent) : QObject(parent)
{

    QString date = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString logPath = QString("%1/%2.json").arg(rootPath).arg(date);
    initWriter(logPath);

    qDebug() << "";
//    connect(this, &VSSLog::outInfo, this, &VSSLog::onWrite);
//    connect(this, &VSSLog::outWarn, this, &VSSLog::onWrite);
//    connect(this, &VSSLog::outError, this, &VSSLog::onWrite);

}

void VSSLog::initWriter(const QString& path)
{
    logFile.setFileName(path);

    if(!logFile.exists())
    {
        if(logFile.open(QIODevice::WriteOnly))
       {
           logFile.close();
//           qDebug() << "file created";
       }
    }

    if(!logFile.open(QIODevice::ReadOnly))
    {
        return;
    }

    QByteArray data = logFile.readAll();
    logFile.close();

    if(data.isEmpty())
    {
        QJsonObject rootObj;
        rootObj["id"] = 1;
        rootObj["scenes"] = QJsonArray();

        QJsonDocument doc = QJsonDocument(rootObj);

        if(!logFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return;
        }

        logFile.write(doc.toJson(QJsonDocument::Indented));
        logFile.close();
    }
}


void VSSLog::addLog(const QString& sensorName, const QString& answer)
{
    QJsonObject newObj;
    newObj["frame_id"] = 1;
    newObj["video_length"] = 10;
    newObj["answer"] = answer;


    if(!sensors.contains(sensorName))
    {
        if(!currObj.isEmpty())
            write(currObj);


        sensors.insert(sensorName);
        currObj["scene"] = sensorName;
        currObj["frames"] = QJsonArray();

    }else{
        QJsonArray frameList = currObj["frames"].toArray();
        frameList.append(newObj);
        currObj["frames"] = frameList;
    }
}


void VSSLog::write(const QJsonObject& obj)
{
    logFile.setFileName("/home/cscho/vss/log.json");
    if(!logFile.open(QIODevice::ReadOnly))
    {
        qCritical() << "[ERROR] Fail to open log file";
        return;
    }

    QByteArray data = logFile.readAll();

    logFile.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject rootObj = doc.object();


    QJsonArray scenes = rootObj["scenes"].toArray();
    scenes.append(obj);

    rootObj["scenes"] = scenes;

    QJsonDocument newDoc(rootObj);

    logFile.open(QIODevice::WriteOnly | QIODevice::Truncate);

    logFile.write(newDoc.toJson(QJsonDocument::Indented));
    logFile.close();

    qDebug() << "";
}

