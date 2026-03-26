#pragma once
#include <QNetworkAccessManager>
#include <QDir>
#include <QDebug>
#include <QObject>

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
    QNetworkAccessManager manager;
    QString dirPath;

    QString modelId;
    QString videoId;
};
