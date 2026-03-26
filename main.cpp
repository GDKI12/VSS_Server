#include <QCoreApplication>
#include "server.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    VideoServer server1(5000);
    VideoServer server2(5001);
    VideoServer server3(5002);
    VideoHandler handler;

    // handler.uploadVideo("/home/cscho/Downloads/20260316_094915_accident_2.mp4");
    // handler.getModel();
    // handler.uploadVideo("/home/cscho/Downloads/warehouse_multistream_1.mp4");

    // handler.summarize();
    // handler.qna();

    return a.exec();
}
