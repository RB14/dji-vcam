// Live-view pipeline: camera session -> access units -> decoder thread -> frames for the UI.
#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "djivcam/decoder.h"
#include "djivcam/h264.h"
#include "djivcam/session.h"

namespace djivcam::vcam {
class VirtualCamera;
}

class Pipeline : public QObject {
    Q_OBJECT

public:
    explicit Pipeline(QObject* parent = nullptr);
    ~Pipeline() override;

    void start(djivcam::SessionConfig config, djivcam::media::DecoderPreference decoder);
    void stop();
    bool running() const { return session_ != nullptr; }

    // The most recent decoded frame (null if none). Taking it re-arms frameAvailable().
    QImage takeLatestFrame();
    // Decoded frames are also published to this virtual camera (may be null). Set while stopped.
    void setVirtualCamera(djivcam::vcam::VirtualCamera* camera) { virtual_camera_ = camera; }

signals:
    // Emitted when a new frame is waiting and the previous notification was consumed, so a slow
    // UI skips frames instead of queueing them (which would add ever-growing latency).
    void frameAvailable();
    void stateChanged(const QString& state, const QString& detail);
    void statsUpdated(double fps, double kbps, double loss_percent, quint64 recovered, quint64 reconnects);
    // Size of the decoded video (changes e.g. 1280x720 -> 960x720 with the camera's aspect ratio).
    void formatChanged(int width, int height);
    void decoderChanged(const QString& backend, bool hardware);
    void errorOccurred(const QString& message);

private:
    void enqueue(djivcam::h264::AccessUnit&& unit);
    void decode_loop(std::stop_token stop, djivcam::media::DecoderPreference preference);
    void report_stats();

    std::unique_ptr<djivcam::LiveViewSession> session_;
    std::unique_ptr<djivcam::h264::AccessUnitAssembler> assembler_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<djivcam::h264::AccessUnit> queue_;
    std::jthread decoder_;
    QTimer stats_timer_;
    std::atomic<std::uint64_t> decoded_frames_{0};
    std::mutex frame_mutex_;
    QImage latest_frame_;
    bool frame_notified_ = false;
    std::uint64_t last_frames_ = 0;
    std::uint64_t last_video_bytes_ = 0;
    std::uint64_t last_video_datagrams_ = 0;
    std::uint64_t last_lost_ = 0;
    djivcam::vcam::VirtualCamera* virtual_camera_ = nullptr;
    int width_ = 0;   // decode thread only
    int height_ = 0;
};
