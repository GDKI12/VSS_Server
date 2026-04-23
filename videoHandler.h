#pragma once
#include <QNetworkAccessManager>
#include <QDir>
#include <QDebug>
#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QElapsedTimer>


// ================= URL =================
const QString VSS_URL = "http://localhost:8100";

const QString HEALTH_ENDPOINT      = VSS_URL + "/health/ready";
const QString MODEL_ENDPOINT       = VSS_URL + "/models";
const QString UPLOAD_FILE_ENDPOINT = VSS_URL + "/files";
const QString GET_FILES_ENDPOINT   = VSS_URL + "/files";
const QString SUMMARIZE_ENDPOINT   = VSS_URL + "/summarize";
const QString QNA_ENDPOINT         = VSS_URL + "/chat/completions";

const QString MODEL_ID = "Cosmos-Reason2-8B";

const QString CONFIG_FILE = "/home/cscho/VSS_Server/config/config.toml";

class VideoHandler : public QObject
{
    Q_OBJECT

public:
    explicit VideoHandler(QObject* parent = nullptr);

    void initialize();

    void requestHealth();
    void getModel();
    void getFiles();

    void enqueueUpload(const QString& videoPath);

signals:
    void uploadFinished(const QString& videoPath, const QString& videoId);
    void uploadFailed(const QString& videoPath, const QString& reason);
    void requestToSend(const QString& videoPath, const QString& answer);

private:
    void startNextUpload();
    void uploadVideo(const QString& videoPath);
    void summarize(const QString& videoPath);
    void qna(const QString& videoPath);

    QNetworkRequest makeRequest(const QString& url);

private:
    QNetworkAccessManager* manager;

    QQueue<QString> m_uploadQueue;
    bool m_uploading;

    QString currentVideoPath;
    QString videoId;
    QString modelId;

    QString vlmPrompt;
    QString captionSummari;
    QString aggre;
    QString query;
};

