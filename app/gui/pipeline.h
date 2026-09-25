// Live-view pipeline: camera session -> access units -> decoder thread -> frames for the UI.
//
// A recorded stream can stand in for the camera (startReplay), which exercises everything after
// the network (decoding, preview, virtual camera) without a camera.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "djivcam/camera_controller.h"
#include "djivcam/decoder.h"
#include "djivcam/h264.h"
#include "djivcam/session.h"

namespace djivcam::vcam {
class VirtualCamera;
}

// Once-a-second figures for the status bar.
struct LiveStats {
    double fps = 0;           // decoded frames
    double kbps = 0;          // H.264 stream
    double delay_ms = 0;      // worst time a frame spent inside the app (queue + decode + hand-over)
    double held = 0;          // frames not shown because video before them was lost
    double loss_percent = 0;  // video datagrams never received (live only)
    quint64 recovered = 0;    // gaps filled by late or re-sent datagrams
    quint64 duplicates = 0;   // datagrams received twice (the camera re-sending)
    quint64 reconnects = 0;
};

class Pipeline : public QObject {
    Q_OBJECT

public:
    explicit Pipeline(QObject* parent = nullptr);
    ~Pipeline() override;

    void start(djivcam::SessionConfig config, djivcam::media::DecoderPreference decoder);
    // Plays a recorded Annex-B H.264 file (e.g. from dji-vcam-cli --dump) in a loop at the camera's
    // 30 fps instead of connecting to the camera.
    void startReplay(const QString& path, djivcam::media::DecoderPreference decoder);
    void stop();
    bool running() const { return session_ != nullptr || replay_.joinable(); }

    using Frame = std::shared_ptr<const djivcam::media::Nv12Frame>;

    // The most recent decoded frame (null if none). Taking it re-arms frameAvailable().
    Frame takeLatestFrame();
    // Decoded frames are also published to this virtual camera (may be null). Set while stopped.
    void setVirtualCamera(djivcam::vcam::VirtualCamera* camera) { virtual_camera_ = camera; }
    // After lost video, keep showing the last intact frame until the next keyframe instead of the
    // damaged frames in between (the camera never re-sends lost video).
    void setHoldOnLoss(bool hold) { hold_on_loss_ = hold; }
    // Lets the datalink start new connections, or holds them (see SessionConfig::connect_allowed).
    void setConnectAllowed(bool allowed);

    // The camera's settings while connected live (null otherwise, e.g. during a replay).
    djivcam::camera::CameraController* camera() const { return camera_.get(); }
    // The camera's latest state. Taking it re-arms cameraChanged().
    djivcam::camera::CameraState takeCameraState();

signals:
    // Emitted when a new frame is waiting and the previous notification was consumed, so a slow
    // UI skips frames instead of queueing them (which would add ever-growing latency).
    void frameAvailable();
    void stateChanged(const QString& state, const QString& detail);
    void statsUpdated(const LiveStats& stats);
    // Size of the decoded video (changes e.g. 1280x720 -> 960x720 with the camera's aspect ratio).
    void formatChanged(int width, int height);
    void decoderChanged(const QString& backend, bool hardware);
    void errorOccurred(const QString& message);
    // The camera reported new settings or status (coalesced: call takeCameraState()).
    void cameraChanged();
    // A camera setting or action failed, with the reason.
    void cameraError(const QString& message);

private:
    using Clock = std::chrono::steady_clock;
    struct QueuedUnit {
        djivcam::h264::AccessUnit unit;
        Clock::time_point arrived;
        bool damaged = false;  // decoded but not shown: video before it was lost (or it is itself damaged)
    };

    void start_decoder(djivcam::media::DecoderPreference decoder);
    void enqueue(djivcam::h264::AccessUnit&& unit);
    void decode_loop(std::stop_token stop, djivcam::media::DecoderPreference preference);
    void replay_loop(std::stop_token stop, const std::vector<djivcam::h264::AccessUnit>& units);
    void report_stats();

    std::unique_ptr<djivcam::LiveViewSession> session_;
    std::unique_ptr<djivcam::camera::CameraController> camera_;
    std::atomic<bool> camera_notified_{false};
    // Session thread only: diagnostics of connections that get no video.
    bool video_since_connect_ = false;
    int messages_logged_ = 0;
    std::unique_ptr<djivcam::h264::AccessUnitAssembler> assembler_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<QueuedUnit> queue_;
    std::jthread decoder_;
    std::jthread replay_;
    QTimer stats_timer_;
    std::atomic<std::uint64_t> decoded_frames_{0};
    std::atomic<std::uint64_t> stream_bytes_{0};
    std::atomic<std::int64_t> worst_delay_us_{0};  // since the last report
    std::atomic<bool> hold_on_loss_{false};
    std::atomic<bool> gap_pending_{false};   // lost video: the access unit being assembled is damaged
    std::atomic<bool> holding_{true};        // until the next intact keyframe (also after (re)connecting)
    std::atomic<std::uint64_t> held_frames_{0};
    std::uint64_t last_held_ = 0;
    std::mutex frame_mutex_;
    Frame latest_frame_;
    bool frame_notified_ = false;
    std::uint64_t last_frames_ = 0;
    std::uint64_t last_stream_bytes_ = 0;
    std::uint64_t last_video_datagrams_ = 0;
    std::uint64_t last_lost_ = 0;
    djivcam::vcam::VirtualCamera* virtual_camera_ = nullptr;
    int width_ = 0;   // decode thread only
    int height_ = 0;
};
