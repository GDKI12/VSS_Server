#include "videoHandler.h"

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

VideoHandler::VideoHandler(QObject* parent)
    : QObject(parent), manager(nullptr), m_uploading(false), ctn(0)
{
    connect(this, &VideoHandler::outInfo, this, &VideoHandler::onWrite);
    connect(this, &VideoHandler::outWarn, this, &VideoHandler::onWrite);
    connect(this, &VideoHandler::outError, this, &VideoHandler::onWrite);

    auto data = toml::parse(CONFIG_FILE.toStdString());

    vlmPrompt      = QString::fromStdString(toml::find<std::string>(data, "Vlm", "content"));
    captionSummari = QString::fromStdString(toml::find<std::string>(data, "Caption", "content"));
    aggre          = QString::fromStdString(toml::find<std::string>(data, "Aggregation", "content"));
    query          = QString::fromStdString(toml::find<std::string>(data, "setting", "query"));
    logPath        = QString::fromStdString(toml::find<std::string>(data, "setting", "log_path"));
    modelId        = QString::fromStdString(toml::find<std::string>(data, "setting", "model_id"));
    chunkSize      = toml::find<int>(data, "setting", "chunk_size");
    QDir().mkpath(logPath);
}

VideoHandler::~VideoHandler()
{
    manager->deleteLater();
}

QString VideoHandler::getLogPath()
{
    return logPath;
}

void VideoHandler::initialize()
{
    if (!manager)
        manager = new QNetworkAccessManager(this);
}

QNetworkRequest VideoHandler::makeRequest(const QString& urlStr)
{
    QNetworkRequest req;
    req.setUrl(QUrl(urlStr));

    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    emit outInfo(QString("REQUEST >> %1").arg((req.url()).toString()));

    return req;
}

void VideoHandler::requestHealth()
{
    QNetworkRequest req = makeRequest(HEALTH_ENDPOINT);

    QNetworkReply* reply = manager->get(req);

    connect(reply, SIGNAL(finished()), this, SLOT(deleteLater()));

    connect(reply, &QNetworkReply::finished, [reply, this]() {
        QString log = "HEALTH >> " + reply->readAll();
        emit outInfo(log);

        reply->deleteLater();
    });
}

void VideoHandler::getModel()
{
    QNetworkRequest req = makeRequest(MODEL_ENDPOINT);

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, [reply, this]() {

        QByteArray data = reply->readAll();

        QString log = "MODEL >> " + data;
        emit outInfo(log);

        if (reply->error() != QNetworkReply::NoError)
        {
            QString log = "MODEL ERROR >> " + reply->errorString();
            emit outWarn(log);

            reply->deleteLater();
            return;
        }

        QJsonArray arr = QJsonDocument::fromJson(data).object()["data"].toArray();

        if (!arr.isEmpty())
            modelId = arr[0].toObject()["id"].toString();

        reply->deleteLater();
    });
}

void VideoHandler::getFiles()
{
    QUrl url(GET_FILES_ENDPOINT);

    QUrlQuery query;
    query.addQueryItem("purpose", "vision");
    url.setQuery(query);

    QNetworkRequest req;
    req.setUrl(url);

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, [reply, this]() {

        QString log = "FILES >> " + reply->readAll();
        emit outInfo(log);

        reply->deleteLater();
    });
}

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
                SLOT(onError(QNetworkReply::NetworkError)));

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath]() {

        QByteArray res = reply->readAll();

        emit outInfo("UPLOAD RESPONSE : " + res);
        emit outInfo(QString("HTTP STATUS %1")
                     .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));


        if (reply->error() != QNetworkReply::NoError)
        {
            emit uploadFailed(videoPath, reply->errorString());
            reply->deleteLater();
            startNextUpload();
            return;
        }

        videoId = QJsonDocument::fromJson(res).object()["id"].toString();

        emit outInfo(QString("Success to upload (videoId = %1)").arg(videoId));

        summarize(videoPath);

        reply->deleteLater();
    });
}

void VideoHandler::summarize(const QString& videoPath)
{
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();


    QJsonObject prompts;
    prompts["vlm_prompt"] = vlmPrompt;
    prompts["summarization"] = captionSummari;
    prompts["aggregation"] = aggre;

    QJsonObject payload;
    payload["id"] = videoId;
    payload["prompt"] = prompts["vlm_prompt"].toString();
    payload["caption_summarization_prompt"] = prompts["summarization"].toString();
    payload["summary_aggregation_prompt"] = prompts["aggregation"].toString();
    payload["model"] = modelId;
    payload["chunk_duration"] = chunkSize;
    payload["chunk_overlap_duration"] = 0;
    payload["summarize"] = true;
    payload["enable_chat"] = true;

    QJsonDocument doc = QJsonDocument(payload);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req = makeRequest(SUMMARIZE_ENDPOINT);

    QNetworkReply* reply = manager->post(req, data);

    emit outInfo("Wating for summarize");

    connect(reply,
            static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
            this,
            [reply, this](QNetworkReply::NetworkError code) {

                emit outWarn(QString("Network error occurred: %1 %2")
                                 .arg(code)
                                 .arg(reply->errorString()));

            });

    connect(reply, &QNetworkReply::finished, [reply, timer, videoPath, this](){

        QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);

        QByteArray responseData = reply->readAll();

        if(reply->error() != QNetworkReply::NoError)
        {
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            emit outError(QString("Upload error: networkError = %1 errorString = %2 httpStatus = %3 reason = %4")
                          .arg(reply->error())
                          .arg(reply->errorString())
                          .arg(httpStatus)
                          .arg(reason.toString()));

            QString err = QString("Summarize error: networkError=%1 errorString=%2 httpStatus=%3 reason=%4")
                                          .arg(reply->error())
                                          .arg(reply->errorString())
                                          .arg(httpStatus)
                                          .arg(reason.toString());

            emit outError(err);


            emit uploadFailed(currentVideoPath, err);

            reply->deleteLater();
            startNextUpload();
            return;
        }

        quint64 inferTime = timer->elapsed();
        emit outInfo(QString("Completed SUMMARIZE inference time (%1)msec").arg(inferTime));
        reply->deleteLater();

        summTimes.push_back(inferTime);
        ctn++;

        if(ctn == 10)
        {
            if(!summTimes.isEmpty())
            {
                auto result = std::minmax_element(summTimes.cbegin(), summTimes.cend());

                qint64 minValue = *result.first;
                qint64 maxValue = *result.second;

                qint64 sum = 0;

                for(auto itr = summTimes.cbegin(); itr != summTimes.cend(); itr++)
                    sum += *itr;

                qint64 avg = sum / summTimes.size();

                emit outInfo(QString("Summarizes ( Max:%1, Min:%2, Avg:%3 )").arg(maxValue).arg(minValue).arg(avg));

                ctn = 0;
                summTimes.clear();
            }
        }

        qna(videoPath);
        });
}

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
    payload["model"] = modelId;

    QByteArray data = QJsonDocument(payload).toJson();

    QNetworkRequest req = makeRequest(QNA_ENDPOINT);

    QNetworkReply* reply = manager->post(req, data);

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath]() {

        QByteArray res = reply->readAll();

        if (reply->error() != QNetworkReply::NoError)
        {
            emit uploadFailed(videoPath, reply->errorString());
            reply->deleteLater();
            startNextUpload();
            return;
        }

        QJsonArray choices = QJsonDocument::fromJson(res).object()["choices"].toArray();
        QString answer = choices[0].toObject()["message"].toObject()["content"].toString();

        emit outInfo(QString("Get answer from VSS : %1").arg(answer));

        emit requestToSend(videoPath, answer);
        emit uploadFinished(videoPath, videoId);

        reply->deleteLater();
        startNextUpload();
    });
}

void VideoHandler::onError(QNetworkReply::NetworkError error)
{

}

void VideoHandler::onWrite(const QString& content, LogLevel level)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss.zzz");

    if(level == LogLevel::INFO)
        qDebug().noquote() << timestamp << "[INFO] " << content;
    else if(level == LogLevel::WARN)
        qWarning().noquote() << "\033[33m" << timestamp << "[WARN] " << content;
    else if(level == LogLevel::ERROR)
        qCritical().noquote() << "\033[31m" << timestamp << "[ERROR] " << content;

}
