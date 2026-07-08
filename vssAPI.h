#pragma once


#include "define.h"

#include <QTimer>

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
    void requestTest(const QString& videoPath);
    void startNextUpload();

signals:
    void uploadFinished(const QString& videoPath, const QString& videoId);
    void uploadFailed(const QString& videoPath, const QString& reason);
    void requestToSend(const QString& videoPath, const QString& answer);
    void onTest(const QString& videoPath);
private:
    void uploadVideo(const QString& videoPath);
    void summarize(const QString& videoPath);
    void qna(const QString& videoPath);
    VssInfo createPacket(const ClipInfo&);
    QNetworkRequest makeRequest(const QString& url);

private:
    QString vssURL;
    QString healthyEndpoint;
    QString modelEndpoint;
    QString filesEndpoint;
    QString summarizeEndpoint;
    QString qnaEndpoint;

    QMutex mutex;
    QNetworkAccessManager* manager;

    QQueue<QString> m_uploadQueue;
    bool m_uploading;

    QString currentVideoPath;

    QString logPath;

    QString videoId;
    QString modelId;

    QString vlmPrompt;
    QString captionSummari;

    QString cvPrompt;
    QString aggre;
    QString query;

    int maxTokens;
    float temperature;
    float topP;
    float topK;
    int summarBatchSize;
    int summarMaxTokens;
    int chunkSize;
    int numFramesPerChunk;
    int chunkOverlapDuration;
    int vlmInputWidth;
    int vlmInputHeight;

    bool enableChat;
    bool enableCVmeta;

    QVector<qint64> summTimes;

    int ctn;
};

