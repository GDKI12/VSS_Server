#pragma once


#include "define.h"

class VssAPI : public QObject
{
    Q_OBJECT

public:
    explicit VssAPI(QObject* parent = nullptr);
    ~VssAPI();
    QString getLogPath();
    void requestHealth();
    void getModel();
    void getFiles();

    void enqueueUpload(const QString& videoPath);

public slots:
    void initialize();
    void onError(QNetworkReply::NetworkError error);

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

    QString logPath;

    QString videoId;
    QString modelId;

    QString vlmPrompt;
    QString captionSummari;
    QString aggre;
    QString query;
    int chunkSize;
    QVector<qint64> summTimes;

    int ctn;
};

