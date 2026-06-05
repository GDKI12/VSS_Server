#include <QCoreApplication>
#include "serverManager.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    ServerManager handler;

    return a.exec();
}
