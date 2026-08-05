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
    void uploadVideo(const QString& videoPath);

signals:
    void uploadFinished(const QString& videoPath, const QString& videoId);
    void uploadFailed(const QString& videoPath, const QString& reason);
    void requestToSend(const QString& videoPath, const QString& answer, int inferTime);
    void onTest(const QString& videoPath);
private:
    void summarize(const QString& videoId, const QString& videoPath);
    void qna(const QString& videoPath);
    VssInfo createPacket(const ClipInfo&);
    QNetworkRequest makeRequest(const QString& url);

private:
    QElapsedTimer timer;
    bool batchTimerFlag = false;

    int camFlag = 0;

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

