#include "videoHandler.h"

#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHttpMultiPart>
#include <QUrlQuery>
#include <QThread>
#include <QProcess>
#include <opencv2/opencv.hpp>
#include <toml.hpp>

// ================= constructor =================
VideoHandler::VideoHandler(QObject* parent)
    : QObject(parent), manager(nullptr), m_uploading(false)
{
    auto data = toml::parse(CONFIG_FILE.toStdString());

    vlmPrompt      = QString::fromStdString(toml::find<std::string>(data, "Vlm", "content"));
    captionSummari = QString::fromStdString(toml::find<std::string>(data, "Caption", "content"));
    aggre          = QString::fromStdString(toml::find<std::string>(data, "Aggregation", "content"));
    query          = QString::fromStdString(toml::find<std::string>(data, "setting", "query"));
}

// ================= init =================
void VideoHandler::initialize()
{
    if (!manager)
        manager = new QNetworkAccessManager(this);
}

// ================= request helper =================
QNetworkRequest VideoHandler::makeRequest(const QString& urlStr)
{
    QNetworkRequest req;
    req.setUrl(QUrl(urlStr));  // Qt5 안정 방식

    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "[REQUEST]" << req.url();

    return req;
}

// ================= health =================
void VideoHandler::requestHealth()
{
    QNetworkRequest req = makeRequest(HEALTH_ENDPOINT);

    QNetworkReply* reply = manager->get(req);

    connect(reply, SIGNAL(finished()), this, SLOT(deleteLater()));

    connect(reply, &QNetworkReply::finished, [reply]() {
        qDebug() << "[HEALTH]" << reply->readAll();
        reply->deleteLater();
    });
}

// ================= model =================
void VideoHandler::getModel()
{
    QNetworkRequest req = makeRequest(MODEL_ENDPOINT);

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, [reply, this]() {

        QByteArray data = reply->readAll();
        qDebug() << "[MODEL]" << data;

        if (reply->error() != QNetworkReply::NoError)
        {
            qWarning() << "[MODEL ERROR]" << reply->errorString();
            reply->deleteLater();
            return;
        }

        QJsonArray arr = QJsonDocument::fromJson(data).object()["data"].toArray();

        if (!arr.isEmpty())
            modelId = arr[0].toObject()["id"].toString();

        reply->deleteLater();
    });
}

// ================= files =================
void VideoHandler::getFiles()
{
    QUrl url(GET_FILES_ENDPOINT);

    QUrlQuery query;
    query.addQueryItem("purpose", "vision");
    url.setQuery(query);

    QNetworkRequest req;
    req.setUrl(url);

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, [reply]() {
        qDebug() << "[FILES]" << reply->readAll();
        reply->deleteLater();
    });
}

// ================= upload queue =================
void VideoHandler::enqueueUpload(const QString& videoPath)
{
    m_uploadQueue.enqueue(videoPath);

    if (m_uploading)
        return;

    m_uploading = true;
    startNextUpload();
}

void VideoHandler::startNextUpload()
{
    if (m_uploadQueue.isEmpty())
    {
        m_uploading = false;
        return;
    }

    currentVideoPath = m_uploadQueue.dequeue();
    uploadVideo(currentVideoPath);
}

// ================= upload =================
void VideoHandler::uploadVideo(const QString& videoPath)
{
    initialize();

    QFile* video = new QFile(videoPath);

    QNetworkRequest req(UPLOAD_FILE_ENDPOINT);
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    if (!video->open(QIODevice::ReadOnly))
    {
        emit uploadFailed(videoPath, "file open failed");
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

    QNetworkReply* reply = manager->post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply,
                SIGNAL(error(QNetworkReply::NetworkError)),
                this,
                SLOT());

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath]() {

        QByteArray res = reply->readAll();

        qDebug() << "[UPLOAD RESPONSE]" << res;
        qDebug() << "[HTTP STATUS]"
                 << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();


        if (reply->error() != QNetworkReply::NoError)
        {
            emit uploadFailed(videoPath, reply->errorString());
            reply->deleteLater();
            startNextUpload();
            return;
        }

        videoId = QJsonDocument::fromJson(res).object()["id"].toString();

        qDebug() << "[UPLOAD SUCCESS] videoId =" << videoId;

        summarize(videoPath);

        reply->deleteLater();
    });
}

// ================= summarize =================
void VideoHandler::summarize(const QString& videoPath)
{
    auto timer = std::make_shared<QElapsedTimer>();
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

    QNetworkRequest req = makeRequest(SUMMARIZE_ENDPOINT);

    QNetworkReply* reply = manager->post(req, data);

    qDebug() << "wating for summarize";

    connect(reply,
            static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
            this,
            [reply](QNetworkReply::NetworkError code) {
                qWarning() << "Network error occurred:"
                           << code
                           << reply->errorString();
            });

    connect(reply, &QNetworkReply::finished, [reply, timer, videoPath, this](){

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
            startNextUpload();
            return;
        }

        quint64 inferTime = timer->elapsed();
        qDebug() << "Inference Time : " << int(inferTime);

        qDebug() << "Completed SUMMARIZE";
        reply->deleteLater();

        qna(videoPath);
        });
}

// ================= qna =================
void VideoHandler::qna(const QString& videoPath)
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "user"},
        {"content", query}
    });

    QJsonObject payload;
    payload["id"] = videoId;
    payload["messages"] = messages;
    payload["model"] = MODEL_ID;

    QByteArray data = QJsonDocument(payload).toJson();

    QNetworkRequest req = makeRequest(QNA_ENDPOINT);

    QNetworkReply* reply = manager->post(req, data);

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath]() {

        QByteArray res = reply->readAll();
        qDebug() << "[QNA]" << res;

        if (reply->error() != QNetworkReply::NoError)
        {
            emit uploadFailed(videoPath, reply->errorString());
            reply->deleteLater();
            startNextUpload();
            return;
        }

        QJsonArray choices = QJsonDocument::fromJson(res).object()["choices"].toArray();
        QString answer = choices[0].toObject()["message"].toObject()["content"].toString();

        emit requestToSend(videoPath, answer);
        emit uploadFinished(videoPath, videoId);

        reply->deleteLater();
        startNextUpload();
    });
}
