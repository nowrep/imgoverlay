#include "manager.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Manager manager;
    return app.exec();
}
