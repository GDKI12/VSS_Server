#ifndef APISERVER_H
#define APISERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class APIServer : public QTcpServer
{
    Q_OBJECT
public:
    APIServer(QObject* parent = nullptr);
    void vssHealthyCheck(bool status);
protected:
    void incomingConnection(qintptr socketDescriptor) override;


private:
    bool status;
};

#endif // APISERVER_H
