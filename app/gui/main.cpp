#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <mutex>

#include "djivcam/version.h"
#include "main_window.h"

namespace {

// Everything the app reports (qInfo/qWarning, from any thread) also goes to
// %LOCALAPPDATA%\dji-vcam\dji-vcam\logs\dji-vcam.log, the first thing to look at when it gets stuck.
// The previous run's log is kept as dji-vcam.previous.log.
QFile* g_log = nullptr;
std::mutex g_log_mutex;

void write_log(QtMsgType type, const QMessageLogContext&, const QString& message) {
    static const char* const kLevels[] = {"debug", "warning", "critical", "fatal", "info"};
    const QByteArray line = QStringLiteral("%1 [%2] %3\n")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                     QString::fromLatin1(kLevels[type]), message)
                                .toUtf8();
    std::lock_guard lock(g_log_mutex);
    if (g_log) {
        g_log->write(line);
        g_log->flush();
    }
    fputs(line.constData(), stderr);
}

void open_log() {
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(folder);
    const QString path = folder + QStringLiteral("/dji-vcam.log");
    QFile::remove(folder + QStringLiteral("/dji-vcam.previous.log"));
    QFile::rename(path, folder + QStringLiteral("/dji-vcam.previous.log"));
    g_log = new QFile(path);
    if (!g_log->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete g_log;
        g_log = nullptr;
    }
    qInstallMessageHandler(write_log);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("dji-vcam"));
    QApplication::setOrganizationName(QStringLiteral("dji-vcam"));
    QApplication::setApplicationVersion(QString::fromUtf8(djivcam::kVersion));
    open_log();
    qInfo("DJI VCam %s started", qPrintable(QApplication::applicationVersion()));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DJI Osmo Action live view"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption connect_option(QStringLiteral("connect"), QStringLiteral("Connect to the camera on startup."));
    parser.addOption(connect_option);
    const QCommandLineOption identifier_option(
        QStringLiteral("identifier"),
        QStringLiteral("Import a pairing identifier the camera has already approved (32 hex chars)."), QStringLiteral("id"));
    parser.addOption(identifier_option);
    const QCommandLineOption replay_option(
        QStringLiteral("replay"),
        QStringLiteral("Play a recorded H.264 stream (dji-vcam-cli --dump) instead of the camera, e.g. to test the "
                       "virtual camera."),
        QStringLiteral("file"));
    parser.addOption(replay_option);
    parser.process(app);

    MainWindow window;
    if (parser.isSet(identifier_option)) {
        window.importPairingIdentifier(parser.value(identifier_option));
    }
    window.show();
    if (parser.isSet(replay_option)) {
        window.replayFile(parser.value(replay_option));
    } else if (parser.isSet(connect_option)) {
        window.connectCamera();
    }
    return QApplication::exec();
}
