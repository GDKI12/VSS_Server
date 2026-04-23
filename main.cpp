#include <QCoreApplication>
#include "server.h"
#include "videoHandler.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    VideoServer server1("cam1", 5000);
    VideoServer server2("cam2", 5001);
    VideoServer server3("cam3", 5002);

//    VideoHandler h;
//    h.getModel();

    return a.exec();
}
