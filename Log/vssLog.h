#ifndef EVALPERFOMANCE_H
#define EVALPERFOMANCE_H


#include "define.h"

class VSSLog : public QObject
{
    Q_OBJECT
public:
    explicit VSSLog(const QString& rootPath, QObject* parent = nullptr);
    void initWriter(const QString& path);
    void setLogFileName(const QString&);

public slots:
    void addLog(QMap<QString, QString> log);
private:
    QFile logFile;
    QString logPath;

};

#endif // EVALPERFOMANCE_H
