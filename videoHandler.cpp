#include "videoHandler.h"

#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHttpMultiPart>
#include <QUrlQuery>
#include <QElapsedTimer>

VideoHandler::VideoHandler(QObject* parent) : QObject(parent)
{

}


void VideoHandler::enqueueUpload(const QString& videoPath)
{
    m_uploadQueue.enqueue(videoPath);

    qDebug() << "[VideoHandler] queued: " << videoPath
             << "queue size = " << m_uploadQueue.size();

    if(m_uploading)
    {
        return;
    }

    m_uploading = true;
    startNextUpload();
}

void VideoHandler::startNextUpload()
{
    if(m_uploadQueue.isEmpty())
    {
        m_uploading = false;
        return;
    }

    currentVideoPath = m_uploadQueue.dequeue();
    qDebug() << "[VideoHandler] start upload" << currentVideoPath;

    uploadVideo(currentVideoPath);
}

void VideoHandler::requestHealty()
{
    QUrl url(HEALTH_ENDPOINT);
    QNetworkRequest req(url);

    manager.get(req);
}

void VideoHandler::getModel()
{
    QUrl url(MODEL_ENDPOINT);
    QNetworkRequest req(url);

    QNetworkReply* reply = manager.get(req);

    QObject::connect(reply, &QNetworkReply::finished, [reply, this](){
        QByteArray data = reply->readAll();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();

        QJsonArray results = obj["data"].toArray();

        QJsonObject result = results[0].toObject();
        modelId = result["id"].toString();

        qDebug() << "modelId: " << modelId;
    });

}

void VideoHandler::getFiles()
{
    // TODO
    QUrl url(GET_FILES_ENDPOINT);
    QNetworkRequest req(url);

    QUrlQuery query;
    query.addQueryItem("purpose", "vision");
    url.setQuery(query);

    QNetworkReply* reply = manager.get(req);

    QObject::connect(reply, &QNetworkReply::finished, [reply, this](){
        QByteArray data = reply->readAll();

        QJsonDocument doc = QJsonDocument::fromJson(data);


        qDebug() << "";
    });

}



void VideoHandler::uploadVideo(QString videoPath)
{
    QFile* video = new QFile(videoPath);

    QNetworkRequest req(UPLOAD_FILE_ENDPOINT);
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    if(!video->open(QIODevice::ReadOnly))
    {
        QString reason = QString("Cannot open video: %1 fileError = %2")
                .arg(videoPath)
                .arg(video->errorString());

        qWarning() << reason;

        delete video;
        delete multiPart;

        emit uploadFailed(videoPath, reason);
        startNextUpload();
        return;
    }

    QFileInfo info(*video);

    QHttpPart purposePart;
    purposePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant("form-data; name=\"purpose\""));
    purposePart.setBody("vision");

    multiPart->append(purposePart);

    QHttpPart mediaPart;
    mediaPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant("form-data; name=\"media_type\""));
    mediaPart.setBody("video");
    multiPart->append(mediaPart);

    QHttpPart filePart;
    QString disposition = QString("form-data; name=\"file\"; filename=\"%1\"").arg(info.fileName());
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
    filePart.setBodyDevice(video);
    video->setParent(multiPart);

    multiPart->append(filePart);

    QNetworkReply* reply = manager.post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply,
            static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
            this,
            [reply](QNetworkReply::NetworkError code) {
                qWarning() << "Network error occurred:"
                           << code
                           << reply->errorString();
            });

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath](){

        QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);


        QByteArray responseData = reply->readAll();

        if(reply->error() != QNetworkReply::NoError)
        {
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString err = QString("Upload error: networkError=%1 errorString=%2 httpStatus=%3 reason=%4")
                                          .arg(reply->error())
                                          .arg(reply->errorString())
                                          .arg(httpStatus)
                                          .arg(reason.toString());

            qWarning() << err;
            emit uploadFailed(videoPath, err);

            reply->deleteLater();
            startNextUpload();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject obj = doc.object();

        videoId = obj["id"].toString();
        qDebug() << "Success to upload file...";
        qDebug() << "videoId: " << videoId;

        summarize();

        reply->deleteLater();

    });
}

void VideoHandler::summarize()
{
    QElapsedTimer *timer = new QElapsedTimer();
    timer->start();


    QJsonObject prompts;
    prompts["vlm_prompt"] = "Write a concise and clear dense caption for the provided warehouse video, focusing on irregular or hazardous events such as boxes falling, workers not wearing PPE, workers falling, workers taking photographs, workers chitchatting, forklift stuck, etc. Start and end each sentence with a time stamp.";
    prompts["summarization"] = "You should summarize the following events of a warehouse in the format start_time:end_time:caption. For start_time and end_time use . to seperate seconds, minutes, hours. If during a time segment only regular activities happen, then ignore them, else note any irregular activities in detail. The output should be bullet points in the format start_time:end_time: detailed_event_description. Don't return anything else except the bullet points.";
    prompts["aggregation"] = "You are a warehouse monitoring system. Given the caption in the form start_time:end_time: caption, Aggregate the following captions in the format start_time:end_time:event_description. If the event_description is the same as another event_description, aggregate the captions in the format start_time1:end_time1,...,start_timek:end_timek:event_description. If any two adjacent end_time1 and start_time2 is within a few tenths of a second, merge the captions in the format start_time1:end_time2. The output should only contain bullet points.  Cluster the output into Unsafe Behavior, Operational Inefficiencies, Potential Equipment Damage and Unauthorized Personnel";

    QJsonObject payload;
    payload["id"] = videoId;
    payload["prompt"] = prompts["vlm_prompt"].toString();
    payload["caption_summarization_prompt"] = prompts["summarization"].toString();
    payload["summary_aggregation_prompt"] = prompts["aggregation"].toString();
    payload["model"] = MODEL_ID;
    payload["chunk_duration"] = 10;
    payload["chunk_overlap_duration"] = 0;
    payload["summarize"] = true;
    payload["enable_chat"] = true;

    QJsonDocument doc = QJsonDocument(payload);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    QUrl url = QUrl(SUMMARIZE_ENDPOINT);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = manager.post(req, data);

    qDebug() << "wating for summarize";

    connect(reply,
            static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
            this,
            [reply](QNetworkReply::NetworkError code) {
                qWarning() << "Network error occurred:"
                           << code
                           << reply->errorString();
            });

    connect(reply, &QNetworkReply::finished, [reply, &timer, this](){

        QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);

        QByteArray responseData = reply->readAll();

        if(reply->error() != QNetworkReply::NoError)
        {
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qWarning() << "Upload error:"
                       << "networkError = " << reply->error()
                       << "errorString = " << reply->errorString()
                       << "httpStatus = " << httpStatus
                       << "reason = " << reason;

            QString err = QString("Summarize error: networkError=%1 errorString=%2 httpStatus=%3 reason=%4")
                                          .arg(reply->error())
                                          .arg(reply->errorString())
                                          .arg(httpStatus)
                                          .arg(reason.toString());

            qWarning() << err;
            emit uploadFailed(currentVideoPath, err);

            reply->deleteLater();
            delete timer;
            startNextUpload();
            return;
        }

        quint64 inferTime = timer->elapsed();
        qDebug() << "Inference Time : " << int(inferTime);

        qDebug() << "Completed SUMMARIZE";
        reply->deleteLater();
        delete timer;

        qna();
    });

}

void VideoHandler::qna()
{
    QJsonArray messages;

    QJsonObject message;
    message["content"] = "is there forklift?";
    message["role"] = "user";

    messages.append(message);

    QJsonObject payload;
    payload["id"] = videoId;
    payload["messages"] = messages;
    payload["model"] = MODEL_ID;

    QJsonDocument doc = QJsonDocument(payload);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    QUrl url = QUrl(QnA_ENDPOINT);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = manager.post(req, data);

    qDebug() << "wating for Q&A";

    connect(reply,
            static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
            this,
            [reply](QNetworkReply::NetworkError code) {
                qWarning() << "Network error occurred:"
                           << code
                           << reply->errorString();
            });

    connect(reply, &QNetworkReply::finished,[reply, this](){

        QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);

        QByteArray responseData = reply->readAll();

        if(reply->error() != QNetworkReply::NoError)
        {
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                        QString err = QString("Q&A error: networkError=%1 errorString=%2 httpStatus=%3 reason=%4")
                                          .arg(reply->error())
                                          .arg(reply->errorString())
                                          .arg(httpStatus)
                                          .arg(reason.toString());

            qWarning() << err;
            emit uploadFailed(currentVideoPath, err);

            reply->deleteLater();
            startNextUpload();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject obj = doc.object();

        QJsonArray choices = obj["choices"].toArray();
        QJsonObject choice = choices[0].toObject();

        QJsonObject contentObj = choice["message"].toObject();

        QString content = contentObj["content"].toString();

        qDebug() << "Complete to Q&A";
        qDebug() << "Answer : " << content;

        emit uploadFinished(currentVideoPath, videoId);

        reply->deleteLater();
        startNextUpload();
    });

}
