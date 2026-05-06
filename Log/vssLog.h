#ifndef EVALPERFOMANCE_H
#define EVALPERFOMANCE_H

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QDateTime>


class VSSLog : public QObject
{
    Q_OBJECT
public:
    explicit VSSLog(const QString& rootPath, QObject* parent = nullptr);
    void initWriter(const QString& path);
    void setLogFileName(const QString&);

public slots:
    void write(const QJsonObject& obj);
    void addLog(const QString& sensorName, const QString& answer);

private:
    QFile logFile;
    QString logPath;
    QSet<QString> sensors;
    QJsonObject currObj;

};

#endif // EVALPERFOMANCE_H
