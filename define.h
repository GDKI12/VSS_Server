#ifndef DEFINE_H
#define DEFINE_H

#include <QObject>
// ================= URL =================
const QString VSS_URL = "http://localhost:8100";

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

struct PTimeInfo
{

};

#endif // DEFINE_H
