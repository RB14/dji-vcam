#include <QApplication>
#include <QCommandLineParser>

#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("dji-vcam"));
    QApplication::setOrganizationName(QStringLiteral("dji-vcam"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DJI Osmo Action live view"));
    parser.addHelpOption();
    const QCommandLineOption connect_option(QStringLiteral("connect"), QStringLiteral("Connect to the camera on startup."));
    parser.addOption(connect_option);
    const QCommandLineOption identifier_option(
        QStringLiteral("identifier"),
        QStringLiteral("Import a pairing identifier the camera has already approved (32 hex chars)."), QStringLiteral("id"));
    parser.addOption(identifier_option);
    parser.process(app);

    MainWindow window;
    if (parser.isSet(identifier_option)) {
        window.importPairingIdentifier(parser.value(identifier_option));
    }
    window.show();
    if (parser.isSet(connect_option)) {
        window.connectCamera();
    }
    return QApplication::exec();
}
