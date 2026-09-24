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

#include "osmolink/decoder.h"
#include "osmolink/h264.h"
#include "osmolink/session.h"

class Pipeline : public QObject {
    Q_OBJECT

public:
    explicit Pipeline(QObject* parent = nullptr);
    ~Pipeline() override;

    void start(osmolink::SessionConfig config, osmolink::media::DecoderPreference decoder);
    void stop();
    bool running() const { return session_ != nullptr; }

    // The most recent decoded frame (null if none). Taking it re-arms frameAvailable().
    QImage takeLatestFrame();

signals:
    // Emitted when a new frame is waiting and the previous notification was consumed, so a slow
    // UI skips frames instead of queueing them (which would add ever-growing latency).
    void frameAvailable();
    void stateChanged(const QString& state, const QString& detail);
    void statsUpdated(double fps, double kbps, double loss_percent, quint64 reconnects);
    void decoderChanged(const QString& backend, bool hardware);
    void errorOccurred(const QString& message);

private:
    void enqueue(osmolink::h264::AccessUnit&& unit);
    void decode_loop(std::stop_token stop, osmolink::media::DecoderPreference preference);
    void report_stats();

    std::unique_ptr<osmolink::LiveViewSession> session_;
    std::unique_ptr<osmolink::h264::AccessUnitAssembler> assembler_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<osmolink::h264::AccessUnit> queue_;
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
};
