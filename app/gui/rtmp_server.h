// A local RTMP server for the camera's RTMP feed: the camera publishes to it over the Wi-Fi network,
// our player and other apps (OBS, VLC) read from it.
//
// An implementation runs a server program bundled next to the app (go2rtc, go2rtc_server.h) as a
// child process, with a configuration generated here, and polls its HTTP API to learn when the
// camera publishes and from which address; another server replaces it by implementing the same few
// methods. Only the RTMP port listens on the network; the player's RTSP port and the API stay on
// this computer.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QProcess;
class QTimer;

class RtmpServer : public QObject {
    Q_OBJECT

public:
    static constexpr quint16 kRtmpPort = 1935;
    static constexpr quint16 kRtspPort = 8554;  // on this computer only
    // The stream's name in every URL: the camera publishes to /live/<key> as DJI Mimo has it do (a
    // short address: the camera takes about 35 characters, live::fits()).
    static QString streamName() { return QStringLiteral("live"); }

    // The server bundled with the app; nullptr if its program is missing.
    static RtmpServer* create(QObject* parent);

    ~RtmpServer() override;

    virtual QString name() const = 0;
    // Starts the server (nothing to do if it runs); false with `error` set if it cannot.
    bool start(QString* error);
    void stop();
    bool running() const;

    // Where the camera publishes: `host` is this computer's address on the camera's network. Without
    // the port, the RTMP default, when the address would be too long for the camera.
    virtual QString ingestUrl(const QString& host, bool with_port = true) const = 0;
    // Where our player reads the stream, on this computer.
    virtual QString playbackUrl() const = 0;
    // Addresses other apps on this computer can open.
    virtual QStringList shareUrls() const = 0;

signals:
    // The camera started publishing (`from`: its address) or stopped (`from` empty).
    void publisherChanged(const QString& from);
    // The server program stopped by itself or could not start.
    void failed(const QString& message);

protected:
    RtmpServer(QString program, QObject* parent);

    // The configuration file's contents and name.
    virtual QByteArray configuration() const = 0;
    virtual QString configFileName() const = 0;
    // The program's arguments, given its configuration file.
    virtual QStringList arguments(const QString& config_path) const = 0;
    // The API request that tells who publishes, and the publisher's address in its answer (empty if
    // none).
    virtual QUrl statusUrl() const = 0;
    virtual QString parsePublisher(const QByteArray& reply) const = 0;

private:
    void poll();

    QString program_;
    QProcess* process_;
    QNetworkAccessManager* network_;
    QTimer* poll_timer_;
    QString publisher_;
    bool stopping_ = false;
};
