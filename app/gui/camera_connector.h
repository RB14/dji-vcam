// Puts the camera on a Wi-Fi network and keeps it there (docs/protocol-notes.md 3.13), on a worker
// thread: finds and pairs it over Bluetooth, switches it to Live Streaming mode and has it join the
// network; on request it also pushes RTMP. It then holds the Bluetooth link with DJI Mimo's
// keep-alive, since the camera leaves the network when the link ends, and starts over when the link
// is lost. stop() returns the camera to Video mode, which sends it back to its own access point.
//
// It reports each stage, so the UI can say what is happening: searching, waiting for the on-camera
// pairing approval, joining.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include "djivcam/live_stream.h"

// A network in the camera's scan list.
struct CameraNetwork {
    QString ssid;
    bool fiveGhz = false;
};
Q_DECLARE_METATYPE(CameraNetwork)

class CameraConnector : public QObject {
    Q_OBJECT

public:
    enum class Stage { Idle, Searching, Pairing, ApprovalNeeded, LiveMode, Scanning, NeedNetwork, Joining, Joined, Failed };
    Q_ENUM(Stage)

    explicit CameraConnector(QObject* parent = nullptr);
    ~CameraConnector() override;

    static bool bluetoothAvailable();

    // identifier: pairing identifier (approved once on the camera); address: preferred camera
    // (empty = first DJI camera found). Without a network (empty ssid) it scans, reports
    // networksFound() and waits for setNetwork().
    void start(const QString& identifier, const QString& token, const QString& address, const QString& ssid,
               const QString& password);
    // The network to join; while joined, the camera leaves the current one and joins this.
    void setNetwork(const QString& ssid, const QString& password);
    // Scans again and reports networksFound() (e.g. for the network dialog's Rescan button).
    void rescan();
    // Starts pushing RTMP while joined (streamStarted()). There is no stop without leaving the
    // network: to go back, setNetwork() again (the camera rejoins without the push).
    void startStream(const djivcam::live::StreamSettings& settings);
    void stop();

signals:
    void stageChanged(CameraConnector::Stage stage, const QString& detail);
    // `model`: the model byte of its advertisement (djivcam/camera_model.h).
    void cameraFound(const QString& name, const QString& address, int model);
    void networksFound(const QList<CameraNetwork>& networks);
    // The join failed (a wrong password, a network out of reach); waiting for setNetwork().
    void joinFailed(const QString& ssid);
    // The camera is on the network; its Wi-Fi MAC finds its address there.
    void joined(const QString& ssid, const QString& mac);
    void streamStarted(bool ok);
    // The Bluetooth link was lost: the camera is going back to its access point; starting over.
    void linkLost();

private:
    struct Request {
        std::optional<std::pair<QString, QString>> network;  // ssid, password
        bool rescan = false;
        std::optional<djivcam::live::StreamSettings> stream;
    };

    void run(std::stop_token stop, QString identifier, QString token, QString address);
    // Waits until a request is pending or the time is up; takes it.
    Request take_request(std::stop_token stop, std::chrono::milliseconds wait);

    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable_any requested_;
    Request pending_;
};
