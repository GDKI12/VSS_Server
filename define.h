#ifndef DEFINE_H
#define DEFINE_H

#include <QDateTime>
#include <QObject>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QProcess>
#include <QThread>
#include <QNetworkAccessManager>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QtGlobal>
#include <algorithm>
#include <memory>
#include <iostream>
#include <QRegularExpression>


const QString CONFIG_FILE = "../config/config.toml";

//const QString MODEL_ID = "Cosmos-Reason2-2B";

#ifdef TEST
struct TestData{
    QString weather;
    QString event;
};
#endif
enum class LogLevel{
    INFO, WARN, ERROR
};

Q_DECLARE_METATYPE(LogLevel)
struct InitConfig
{
    int channel;
    int fps;
    int clipLengthSec;
    int targetScenes;

    QString deviceType;

    QList<QString> weather;
    QList<QString> time;
    QList<QString> roadEnv;
    QList<QString> scenario;
};


struct ClipInfo
{
    QString sensorName;
    QString camId;
    QList<QString> weather;
    QList<QString> timeOfDay;
    QList<QString> roadType;
    QList<QString> event;
};

struct ClientContext {
    QString savePath;
    QString requestId;
    QString sensorName;
    QString camId;
    QByteArray inputBuffer;
    bool videoStarted = false;
    quint64 receivedBytes = 0;
    QDateTime startTime;
    QDateTime endTime;
};


class Writter
{
public:
  static void write(const QString& content, LogLevel level = LogLevel::INFO)
  {
      QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss.zzz");

      if(level == LogLevel::INFO)
          qDebug().noquote() << timestamp << "[INFO] " << content;
      else if(level == LogLevel::WARN)
          qWarning().noquote() << "\033[33m" << timestamp << "[WARN] " << content;
      else if(level == LogLevel::ERROR)
          qCritical().noquote() << "\033[31m" << timestamp << "[ERROR] " << content;
  }

  static void info(const QString& content)
  {
      write(content, LogLevel::INFO);
  }

  static void warn(const QString& content)
  {
      write(content, LogLevel::WARN);
  }

  static void error(const QString& content)
  {
      write(content, LogLevel::ERROR);
  }
};

#endif // DEFINE_H
