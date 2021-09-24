#include "manager.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Imgoverlay Client"));
    app.setApplicationVersion(QStringLiteral(IMGOVERLAY_VERSION));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("config"), QStringLiteral("Config file"));

    QCommandLineOption trayOption({QStringLiteral("t"), QStringLiteral("tray")},
                                   QStringLiteral("Start minimized in tray."));

    parser.addOption(trayOption);
    parser.process(app);

    Manager manager(parser.positionalArguments().value(0), parser.isSet(trayOption));
    return app.exec();
}
