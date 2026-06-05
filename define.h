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

// ================= URL =================
//const QString VSS_URL = "http://localhost:8100";
const QString VSS_URL = "http://192.168.0.187:8100";

const QString HEALTH_ENDPOINT      = VSS_URL + "/health/ready";
const QString MODEL_ENDPOINT       = VSS_URL + "/models";
const QString UPLOAD_FILE_ENDPOINT = VSS_URL + "/files";
const QString GET_FILES_ENDPOINT   = VSS_URL + "/files";
const QString SUMMARIZE_ENDPOINT   = VSS_URL + "/summarize";
const QString QNA_ENDPOINT         = VSS_URL + "/chat/completions";

const QString CONFIG_FILE = "/home/cscho/VSS_Server/config/config.toml";

//const QString MODEL_ID = "Cosmos-Reason2-2B";

enum class LogLevel{
    INFO, WARN, ERROR
};

Q_DECLARE_METATYPE(LogLevel)


struct ClipInfo
{
    QString camName;
    QString sensorName;
    QString videoPath;
    QStringList weather;
    QStringList event;
};

struct ClientContext {
    QString savePath;
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
