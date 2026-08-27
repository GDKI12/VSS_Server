#include "apiserver.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "define.h"

APIServer::APIServer(QObject* parent) : QTcpServer(parent)
{
    Writter::info("VSS API Server opend");
    status = false;
}

void APIServer::vssHealthyCheck(bool status)
{
    this->status = status;
}

void APIServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket* socket = new QTcpSocket(this);

    if(!socket->setSocketDescriptor(socketDescriptor))
    {
        socket->deleteLater();
        return;
    }

    connect(socket, &QTcpSocket::readyRead, this, [this, socket](){
       QByteArray request = socket->readAll();

       QByteArray firstLine = request.split('\n').first().trimmed();

       if(firstLine.startsWith("GET /vss/healthy "))
       {

           Writter::info("GET /vss/health");
           QJsonObject obj;
           obj["success"] = status;
           obj["message"] = "VSS agent status";

           QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

           QByteArray response;

           response += "HTTP/1.1 200 OK\r\n";
           response += "Content-Type: application/json; charset=utf-8\r\n";
           response += "Content-Length: "
                    + QByteArray::number(body.size())
                    + "\r\n";
           response += "Connection: close\r\n";
           response += "\r\n";
           response += body;

           socket->write(response);
           socket->disconnectFromHost();
       }else{
           QByteArray body = "Not Found";

           QByteArray response;

           response += "HTTP/1.1 404 Not Found\r\n";
           response += "Content-Type: text/plain\r\n";
           response += "Content-Length: "
                    + QByteArray::number(body.size())
                    + "\r\n";
           response += "Connection: close\r\n";
           response += "\r\n";
           response += body;

           socket->write(response);
           socket->disconnectFromHost();
       }
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

}
