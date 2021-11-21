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

    QCommandLineOption shmOption({QStringLiteral("s"), QStringLiteral("shm")},
                                  QStringLiteral("Use shared memory instead of DMA-BUF."));

    QCommandLineOption disableGpuOption(QStringLiteral("disable-gpu"),
                                        QStringLiteral("Disable QtWebEngine GPU rendering."));

    parser.addOption(trayOption);
    parser.addOption(shmOption);
    parser.addOption(disableGpuOption);
    parser.process(app);

    Manager manager(parser.positionalArguments().value(0), parser.isSet(trayOption), parser.isSet(shmOption));
    return app.exec();
}
