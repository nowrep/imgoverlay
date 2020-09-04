#include "manager.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Imgoverlay Client"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("config"), QStringLiteral("Config file"));

    QCommandLineOption trayOption({QStringLiteral("t"), QStringLiteral("tray")},
                                   QStringLiteral("Start minimized in tray."));

    parser.addOption(trayOption);
    parser.process(app);

    Manager manager(parser.positionalArguments().value(0), parser.isSet(trayOption));
    return app.exec();
}
