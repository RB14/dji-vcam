#include <QApplication>
#include <QCommandLineParser>

#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("osmolink"));
    QApplication::setOrganizationName(QStringLiteral("osmolink"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DJI Osmo Action live view"));
    parser.addHelpOption();
    const QCommandLineOption connect_option(QStringLiteral("connect"), QStringLiteral("Connect to the camera on startup."));
    parser.addOption(connect_option);
    const QCommandLineOption identifier_option(
        QStringLiteral("identifier"),
        QStringLiteral("Pairing identifier the camera has approved (32 hex chars)."), QStringLiteral("id"));
    parser.addOption(identifier_option);
    parser.process(app);

    MainWindow window;
    if (parser.isSet(identifier_option)) {
        window.setPairingIdentifier(parser.value(identifier_option));
    }
    window.show();
    if (parser.isSet(connect_option)) {
        window.connectCamera();
    }
    return QApplication::exec();
}
