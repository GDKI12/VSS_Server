#include "vssAPI.h"

#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHttpMultiPart>
#include <QUrlQuery>
#include <QThread>
#include <QProcess>
#include <opencv2/opencv.hpp>
#include <QTimer>
#include <toml.hpp>

VssAPI::VssAPI(QObject* parent)
    : QObject(parent), manager(nullptr), m_uploading(false), ctn(0)
{
    auto data      = toml::parse(CONFIG_FILE.toStdString());
    vssURL         = QString::fromStdString(toml::find<std::string>(data, "setting", "vss_url"));

    maxTokens              = toml::find<int>(data, "summarize", "max_tokens");
    temperature            = toml::find<float>(data, "summarize", "temperature");
    topP                   = toml::find<float>(data, "summarize", "top_p");
    topK                   = toml::find<float>(data, "summarize", "top_k");
    summarBatchSize        = toml::find<int>(data, "summarize", "summarize_batch_size");
    summarMaxTokens        = toml::find<int>(data, "summarize", "summarize_max_tokens");
    chunkSize              = toml::find<int>(data, "summarize", "chunk_duration");
    numFramesPerChunk      = toml::find<int>(data, "summarize", "num_frames_per_chunk");
    chunkOverlapDuration   = toml::find<int>(data, "summarize", "chunk_overlap_duration");
    vlmInputWidth          = toml::find<int>(data, "summarize", "vlm_input_width");
    vlmInputHeight         = toml::find<int>(data, "summarize", "vlm_input_height");

    enableChat     = toml::find<bool>(data, "summarize", "enable_chat");
    enableCVmeta   = toml::find<bool>(data, "summarize", "enable_cv_meta");

    vlmPrompt      = QString::fromStdString(toml::find<std::string>(data, "Vlm", "content"));
    captionSummari = QString::fromStdString(toml::find<std::string>(data, "Caption", "content"));
    aggre          = QString::fromStdString(toml::find<std::string>(data, "Aggregation", "content"));
    query          = QString::fromStdString(toml::find<std::string>(data, "setting", "query"));
    logPath        = QString::fromStdString(toml::find<std::string>(data, "setting", "log_path"));
    cvPrompt       = QString::fromStdString(toml::find<std::string>(data, "summarize", "cv_prompt"));


    QDir().mkpath(logPath);

    healthyEndpoint = vssURL + "/health/ready";
    modelEndpoint = vssURL + "/models";
    filesEndpoint = vssURL + "/files";
    summarizeEndpoint = vssURL + "/summarize";
    qnaEndpoint = vssURL + "/chat/completions";
}

VssAPI::~VssAPI()
{
    manager->deleteLater();
}

QString VssAPI::getLogPath()
{
    return logPath;
}

void VssAPI::initialize()
{
    if (!manager)
        manager = new QNetworkAccessManager(this);
#ifndef TEST
    getModel();
#endif
}

QNetworkRequest VssAPI::makeRequest(const QString& urlStr)
{
    QNetworkRequest req;
    req.setUrl(QUrl(urlStr));

    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    Writter::info(QString("REQUEST >> %1").arg((req.url()).toString()));

    return req;
}

void VssAPI::requestHealth()
{
    QNetworkRequest req = makeRequest(healthyEndpoint);

    QNetworkReply* reply = manager->get(req);

    connect(reply, SIGNAL(finished()), this, SLOT(deleteLater()));

    connect(reply, &QNetworkReply::finished, [reply]() {
        QString log = "HEALTH >> " + reply->readAll();
        Writter::info(log);

        reply->deleteLater();
    });
}

void VssAPI::getModel()
{
    QNetworkRequest req = makeRequest(modelEndpoint);

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, [reply, this]() {

        QByteArray data = reply->readAll();

        QString log = "MODEL >> " + data;
        Writter::info(log);

        if (reply->error() != QNetworkReply::NoError)
        {
            QString log = "MODEL ERROR >> " + reply->errorString();
            Writter::warn(log);

            reply->deleteLater();
            return;
        }

        QJsonArray arr = QJsonDocument::fromJson(data).object()["data"].toArray();

        if (!arr.isEmpty())
            modelId = arr[0].toObject()["id"].toString();

        reply->deleteLater();
    });
}

void VssAPI::getFiles()
{
    QUrl url(filesEndpoint);

    QUrlQuery query;
    query.addQueryItem("purpose", "vision");
    url.setQuery(query);

    QNetworkRequest req;
    req.setUrl(url);

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, [reply]() {

        QString log = "FILES >> " + reply->readAll();
        Writter::info(log);

        reply->deleteLater();
    });
}

void VssAPI::enqueueUpload(const QString& videoPath)
{
    QMutexLocker locker(&mutex);
    m_uploadQueue.enqueue(videoPath);

    if (m_uploading)
        return;

    m_uploading = true;
    startNextUpload();
}

void VssAPI::startNextUpload()
{
    if (m_uploadQueue.isEmpty())
    {
        m_uploading = false;
        return;
    }

    currentVideoPath = m_uploadQueue.dequeue();
    Writter::info(QString("Processing vss for %1").arg(currentVideoPath));
#ifdef TEST
    requestTest(currentVideoPath);

#else
    uploadVideo(currentVideoPath);
#endif
}

void VssAPI::requestTest(const QString& videoPath)
{
    initialize();
    emit onTest(videoPath);
}

void VssAPI::uploadVideo(const QString& videoPath)
{
    if(!batchTimerFlag)
    {
        timer.start();
        batchTimerFlag = true;
    }

    initialize();

    QFile* video = new QFile(videoPath);

    QNetworkRequest req(filesEndpoint);
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
            QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            this,
            &VssAPI::onError);

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath]() {

        QByteArray res = reply->readAll();

        Writter::info("UPLOAD RESPONSE : " + res);
        Writter::info(QString("HTTP STATUS %1")
                     .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));


        if (reply->error() != QNetworkReply::NoError)
        {
            emit uploadFailed(videoPath, reply->errorString());
            reply->deleteLater();
            startNextUpload();
            return;
        }

        QString videoId = QJsonDocument::fromJson(res).object()["id"].toString();

        Writter::info(QString("Success to upload (videoId = %1)").arg(videoId));

        summarize(videoId, videoPath);

        reply->deleteLater();
    });
}

void VssAPI::summarize(const QString& videoId, const QString& videoPath)
{
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
    payload["num_frames_per_chunk"] = numFramesPerChunk;
    payload["vlm_input_width"] = vlmInputWidth;
    payload["vlm_input_height"] = vlmInputHeight;
    payload["chunk_overlap_duration"] = chunkOverlapDuration;
    payload["summarize"] = true;
    payload["enable_cv_metadata"] = enableCVmeta;
    payload["enable_chat"] = enableChat;
    payload["cv_pipeline_prompt"] = cvPrompt;

    payload["max_tokens"] = maxTokens;
    payload["temperature"] = temperature;
    payload["top_p"] = topP;
    payload["top_k"] = topK;
    payload["summarize_batch_size"] = summarBatchSize;
    payload["summarize_max_tokens"] = summarMaxTokens;


    QJsonDocument doc = QJsonDocument(payload);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req = makeRequest(summarizeEndpoint);

    QNetworkReply* reply = manager->post(req, data);

    Writter::info("Wating for summarize");

    connect(reply,
            static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
            this,
            [reply](QNetworkReply::NetworkError code) {

                Writter::warn(QString("Network error occurred: %1 %2")
                                 .arg(code)
                                 .arg(reply->errorString()));

            });

    connect(reply, &QNetworkReply::finished, [reply, videoPath, this](){

        QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);

        QByteArray responseData = reply->readAll();
        QJsonObject rootObj = QJsonDocument::fromJson(responseData).object();
        QJsonArray choices = rootObj["choices"].toArray();
        QString summarizeId = rootObj["id"].toString();
        QJsonObject choice = choices[0].toObject();
        QJsonObject message = choice["message"].toObject();
        QString content = message["content"].toString();

        QJsonObject usage = rootObj["usage"].toObject();
        int processingTime = usage["query_processing_time"].toInt();

        Writter::info(QString("Summarazing infer time : %1 sec").arg(processingTime));

        if(reply->error() != QNetworkReply::NoError)
        {
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            Writter::error(QString("Upload error: networkError = %1 errorString = %2 httpStatus = %3 reason = %4")
                          .arg(reply->error())
                          .arg(reply->errorString())
                          .arg(httpStatus)
                          .arg(reason.toString()));

            QString err = QString("Summarize error: networkError=%1 errorString=%2 httpStatus=%3 reason=%4")
                                          .arg(reply->error())
                                          .arg(reply->errorString())
                                          .arg(httpStatus)
                                          .arg(reason.toString());

            Writter::error(err);


            emit uploadFailed(currentVideoPath, err);

            reply->deleteLater();
//            startNextUpload();
            return;
        }

        reply->deleteLater();

        summTimes.push_back(processingTime);
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

                Writter::info(QString("Summarizes ( Max:%1, Min:%2, Avg:%3 )").arg(maxValue).arg(minValue).arg(avg));

                ctn = 0;
                summTimes.clear();
            }
        }

        camFlag++;

        if(camFlag == 3)
        {
            quint64 elapsedMs = timer.elapsed();
            Writter::info(QString("Total Summarzie time is %1").arg(elapsedMs));
            camFlag=0;
            batchTimerFlag = false;
        }
        Writter::info(QString("End to Summarize of %1").arg(summarizeId));
        emit requestToSend(videoPath, content, processingTime);
//        startNextUpload();
        });
}

void VssAPI::qna(const QString& videoPath)
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "user"},
        {"content", query}
    });

    QJsonObject payload;
//    payload["id"] = videoId;
    payload["messages"] = messages;
    payload["model"] = modelId;

    QByteArray data = QJsonDocument(payload).toJson();

    QNetworkRequest req = makeRequest(qnaEndpoint);

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

        Writter::info(QString("Get answer from VSS : %1").arg(answer));

//        emit uploadFinished(videoPath, videoId);

        reply->deleteLater();
        startNextUpload();
    });
}

void VssAPI::onError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error);
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply)
        Writter::error(reply->errorString());
}


