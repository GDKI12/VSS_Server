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

    if (!video->open(QIODevice::ReadOnly))
    {
        emit uploadFailed(videoPath, "file open failed");
        return;
    }

    QFileInfo info(*video);

    QNetworkRequest req;
    req.setUrl(QUrl(UPLOAD_FILE_ENDPOINT));

    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart purposePart;
    purposePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          "form-data; name=\"purpose\"");
    purposePart.setBody("vision");
    multiPart->append(purposePart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"file\"; filename=\"%1\"").arg(info.fileName()));
    filePart.setBodyDevice(video);

    video->setParent(multiPart);
    multiPart->append(filePart);

    QNetworkReply* reply = manager->post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath]() {

        QByteArray res = reply->readAll();
        qDebug() << "[UPLOAD]" << res;

        if (reply->error() != QNetworkReply::NoError)
        {
            emit uploadFailed(videoPath, reply->errorString());
            reply->deleteLater();
            startNextUpload();
            return;
        }

        videoId = QJsonDocument::fromJson(res).object()["id"].toString();

        summarize(videoPath);

        reply->deleteLater();
    });
}

// ================= summarize =================
void VideoHandler::summarize(const QString& videoPath)
{
    QJsonObject payload;
    payload["id"] = videoId;
    payload["prompt"] = vlmPrompt;
    payload["caption_summarization_prompt"] = captionSummari;
    payload["summary_aggregation_prompt"] = aggre;
    payload["model"] = MODEL_ID;
    payload["summarize"] = true;

    QByteArray data = QJsonDocument(payload).toJson();

    QNetworkRequest req = makeRequest(SUMMARIZE_ENDPOINT);

    QNetworkReply* reply = manager->post(req, data);

    connect(reply, &QNetworkReply::finished, [reply, this, videoPath]() {

        qDebug() << "[SUMMARIZE]" << reply->readAll();

        if (reply->error() != QNetworkReply::NoError)
        {
            emit uploadFailed(videoPath, reply->errorString());
            reply->deleteLater();
            startNextUpload();
            return;
        }

        qna(videoPath);

        reply->deleteLater();
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
