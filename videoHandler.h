#pragma once
#include <QNetworkAccessManager>
#include <QDir>
#include <QDebug>
#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QElapsedTimer>
const QString VSS_URL = "http://localhost:8100";

const QString HEALTH_ENDPOINT = VSS_URL + "/health/ready";
const QString MODEL_ENDPOINT = VSS_URL + "/models";
const QString UPLOAD_FILE_ENDPOINT = VSS_URL + "/files";
const QString GET_FILES_ENDPOINT = VSS_URL + "/files";
const QString SUMMARIZE_ENDPOINT = VSS_URL + "/summarize";
const QString QnA_ENDPOINT = VSS_URL + "/chat/completions";

const QString MODEL_ID = "Cosmos-Reason2-2B";

class VideoHandler : public QObject
{
    Q_OBJECT

public:
    explicit VideoHandler(QObject* parent = nullptr);
    void requestHealty();
    void uploadVideo(QString);
    void summarize();
    void qna();
    void getModel();
    void getFiles();
    void request(QString);

private:
    void startNextUpload();

public slots:
    void enqueueUpload(const QString&);
    void initialize();

signals:
    void uploadFinished(const QString& videoPath, const QString& videoId);
    void uploadFailed(const QString& videoPath, const QString& reson);

private:
    QNetworkAccessManager* manager = nullptr;
    QString dirPath;
    QString modelId;
    QString videoId;
    QString currentVideoPath;

    QQueue<QString> m_uploadQueue;
    bool m_uploading = false;

};
