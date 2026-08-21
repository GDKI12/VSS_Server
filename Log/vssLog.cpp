#include "vssLog.h"

VSSLog::VSSLog(const QString& rootPath, QObject* parent) : QObject(parent)
{

    QString date = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString logPath = QString("%1/%2.json").arg(rootPath).arg(date);
    initWriter(logPath);
}

void VSSLog::initWriter(const QString& path)
{
    logFile.setFileName(path);

    if(!logFile.exists())
    {
        if(logFile.open(QIODevice::WriteOnly))
       {
           logFile.close();
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
            rootObj["scenes"] = QJsonArray();

            QJsonDocument doc = QJsonDocument(rootObj);

            if(!logFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                return;
            }

            logFile.write(doc.toJson(QJsonDocument::Indented));
            logFile.close();
        }

        Writter::info(QString("Success to create log file: %1").arg(path));
    }
}


void VSSLog::addLog(QMap<QString, QString> result)
{

    if(!logFile.open(QIODevice::ReadOnly))
    {
        return;
    }

    QByteArray data = logFile.readAll();
    logFile.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);

    if(!doc.isObject())
    {
        return;
    }
    QJsonObject rootObj = doc.object();

    QJsonArray scenes = rootObj["scenes"].toArray();

    bool found = false;

    for(int i = 0; i < scenes.size(); i++)
    {
        QJsonObject sceneObj = scenes[i].toObject();

        if(sceneObj["scene"].toString() != result["sensorName"])
            continue;

        QJsonArray frames = sceneObj["frames"].toArray();

        QJsonObject newObj;

        for(auto itr = result.constBegin(); itr != result.constEnd(); itr++)
        {
            if(itr.key() == "sensorName")
                continue;

            newObj[itr.key()] = itr.value();
        }

        frames.append(newObj);
        sceneObj["frames"] = frames;
        scenes[i] = sceneObj;

        found = true;
        break;
    }

    if(!found)
    {
        QJsonObject newObj;

        for(auto itr = result.constBegin(); itr != result.constEnd(); itr++)
        {
            if(itr.key() == "sensorName")
                continue;

            newObj[itr.key()] = itr.value();
        }

        QJsonArray newFrames;
        newFrames.append(newObj);

        QJsonObject newScene;
        newScene["scene"] = result["sensorName"];
        newScene["frames"] = newFrames;

        scenes.append(newScene);
    }

    rootObj["scenes"] = scenes;

    QJsonDocument newDoc = QJsonDocument(rootObj);
    if(!logFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    logFile.write(newDoc.toJson(QJsonDocument::Indented));
    logFile.close();

}


