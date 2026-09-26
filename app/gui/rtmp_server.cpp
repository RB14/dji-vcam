#include "rtmp_server.h"

#include "go2rtc_server.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr int kPollMs = 1000;

}  // namespace

RtmpServer* RtmpServer::create(QObject* parent) {
    // The implementation bundled with the app; another server plugs in here.
#ifdef Q_OS_WIN
    const QString program = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("go2rtc.exe"));
#else
    const QString program = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("go2rtc"));
#endif
    return QFileInfo::exists(program) ? new Go2RtcServer(program, parent) : nullptr;
}

RtmpServer::RtmpServer(QString program, QObject* parent)
    : QObject(parent),
      program_(std::move(program)),
      process_(new QProcess(this)),
      network_(new QNetworkAccessManager(this)),
      poll_timer_(new QTimer(this)) {
    poll_timer_->setInterval(kPollMs);
    connect(poll_timer_, &QTimer::timeout, this, &RtmpServer::poll);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyRead, this, [this] {
        while (process_->canReadLine()) {
            qInfo("rtmp server: %s", process_->readLine().trimmed().constData());
        }
    });
    connect(process_, &QProcess::finished, this, [this](int code) {
        poll_timer_->stop();
        if (!publisher_.isEmpty()) {
            publisher_.clear();
            emit publisherChanged({});
        }
        if (!stopping_) {
            emit failed(tr("The RTMP server (%1) stopped (exit code %2)").arg(name()).arg(code));
        }
    });
}

RtmpServer::~RtmpServer() { stop(); }

bool RtmpServer::start(QString* error) {
    if (running()) {
        return true;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/rtmp");
    QDir().mkpath(dir);
    const QString config = dir + QLatin1Char('/') + configFileName();
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(configuration()) < 0) {
        *error = tr("cannot write %1").arg(config);
        return false;
    }
    file.close();
    stopping_ = false;
    process_->setWorkingDirectory(dir);
    process_->start(program_, arguments(config));
    if (!process_->waitForStarted(5000)) {
        *error = tr("cannot start %1: %2").arg(program_, process_->errorString());
        return false;
    }
    qInfo("rtmp server: %s started", qPrintable(name()));
    poll_timer_->start();
    return true;
}

void RtmpServer::stop() {
    if (process_->state() == QProcess::NotRunning) {
        return;
    }
    stopping_ = true;
    poll_timer_->stop();
    process_->kill();
    process_->waitForFinished(3000);
}

bool RtmpServer::running() const { return process_->state() == QProcess::Running; }

void RtmpServer::poll() {
    QNetworkRequest request(statusUrl());
    request.setTransferTimeout(kPollMs - 100);
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QString from = reply->error() == QNetworkReply::NoError ? parsePublisher(reply->readAll()) : QString();
        if (from != publisher_) {
            publisher_ = from;
            qInfo("rtmp server: %s", from.isEmpty() ? "no publisher" : qPrintable(QStringLiteral("publishing from ") + from));
            emit publisherChanged(from);
        }
    });
}
