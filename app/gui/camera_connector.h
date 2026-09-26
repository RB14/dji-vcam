// Puts the camera on a Wi-Fi network and keeps it there (docs/protocol-notes.md 3.13), on a worker
// thread: finds and pairs it over Bluetooth, switches it to Live Streaming mode and has it join the
// network; on request it also pushes RTMP. It then holds the Bluetooth link with DJI Mimo's
// keep-alive, since the camera leaves the network when the link ends, and starts over when the link
// is lost. stop() returns the camera to Video mode, which sends it back to its own access point.
// On request the link also carries the camera's settings (camera()), for the RTMP feed alone.
//
// It reports each stage, so the UI can say what is happening: searching, waiting for the on-camera
// pairing approval, joining.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "djivcam/camera_controller.h"
#include "djivcam/live_stream.h"

namespace djivcam::live {
class Link;
}

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
    // Ends the session without waiting: the goodbye (the push stopped, Video mode, which sends the
    // camera back to its access point; seconds of Bluetooth round trips) finishes in the background,
    // then stopped(). A start() meanwhile begins once it is done.
    void stop();
    // Waits until a stop() has finished (e.g. before the app exits).
    void wait();

    // The camera's settings over the Bluetooth link, as DJI Mimo changes them during its livestream:
    // for the RTMP feed, where the live view's connection that otherwise carries them does not run.
    // Active while enabled and the camera is on the network.
    djivcam::camera::CameraController* camera() { return camera_.get(); }
    void setCameraControl(bool on);
    // The camera's latest state. Taking it re-arms cameraChanged().
    djivcam::camera::CameraState takeCameraState();

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
    // The camera reported new settings or status (coalesced: call takeCameraState()).
    void cameraChanged();
    // A camera setting failed, with the reason.
    void cameraError(const QString& message);
    // A stopped session has said goodbye (stop()).
    void stopped();

private:
    struct Request {
        std::optional<std::pair<QString, QString>> network;  // ssid, password
        bool rescan = false;
        std::optional<djivcam::live::StreamSettings> stream;
    };

    struct CameraCommand {
        djivcam::camera::Command command;
        djivcam::RequestTracker::ReplyCallback on_reply;
    };

    void run(std::stop_token stop, QString identifier, QString token, QString address);
    // Waits until a request or a camera command is pending or the time is up; takes the request.
    Request take_request(std::stop_token stop, std::chrono::milliseconds wait);
    // Sends the next queued camera command over `camera` (worker thread), one per tick so the
    // keep-alive keeps its pace.
    void send_camera_command(djivcam::live::Link& camera);
    // Answers the queued camera commands with "no reply" (not joined, control off).
    void fail_camera_commands();
    void set_joined(bool joined);

    std::jthread worker_;
    std::jthread finishing_;  // a stopped session saying goodbye; the next worker waits for it
    std::mutex mutex_;
    std::condition_variable_any requested_;
    Request pending_;
    std::deque<CameraCommand> camera_commands_;  // guarded by mutex_
    std::atomic<bool> joined_{false};
    std::atomic<bool> camera_control_{false};
    std::atomic<bool> subscribe_{false};  // the controller's on_streaming() is due
    std::atomic<bool> camera_notified_{false};
    std::unique_ptr<djivcam::camera::CameraController> camera_;
};
