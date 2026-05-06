#pragma once
#include <QNetworkAccessManager>
#include <QDir>
#include <QDebug>
#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QElapsedTimer>
#include <QNetworkReply>

// ================= URL =================
const QString VSS_URL = "http://localhost:8100";

const QString HEALTH_ENDPOINT      = VSS_URL + "/health/ready";
const QString MODEL_ENDPOINT       = VSS_URL + "/models";
const QString UPLOAD_FILE_ENDPOINT = VSS_URL + "/files";
const QString GET_FILES_ENDPOINT   = VSS_URL + "/files";
const QString SUMMARIZE_ENDPOINT   = VSS_URL + "/summarize";
const QString QNA_ENDPOINT         = VSS_URL + "/chat/completions";

const QString MODEL_ID = "Cosmos-Reason2-2B";

const QString CONFIG_FILE = "/home/cscho/VSS_Server/config/config.toml";


enum class LogLevel{
    INFO, WARN, ERROR
};

Q_DECLARE_METATYPE(LogLevel)

class VideoHandler : public QObject
{
    Q_OBJECT

public:
    explicit VideoHandler(QObject* parent = nullptr);
    QString getLogPath();
    void requestHealth();
    void getModel();
    void getFiles();

    void enqueueUpload(const QString& videoPath);

public slots:
    void initialize();
    void onError(QNetworkReply::NetworkError error);

    void onWrite(const QString& content, LogLevel logLevel);

signals:
    void outInfo(const QString& info, LogLevel logLevel = LogLevel::INFO);
    void outWarn(const QString& warn, LogLevel logLevel = LogLevel::WARN);
    void outError(const QString& error, LogLevel logLevel = LogLevel::ERROR);

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

    QString logPath;

    QString videoId;
    QString modelId;

    QString vlmPrompt;
    QString captionSummari;
    QString aggre;
    QString query;
};

