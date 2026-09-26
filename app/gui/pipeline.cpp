#include "pipeline.h"

#include <QFile>
#include <QFileInfo>

#include <exception>
#include <optional>

#include "djivcam/network_stream.h"

#ifdef DJIVCAM_HAVE_VCAM
#include "djivcam/vcam_protocol.h"
#include "djivcam/virtual_camera.h"
#endif
#include <utility>

namespace {

// Access units are never dropped (every P-frame depends on the previous one); this only guards
// against unbounded growth if decoding ever stalls.
constexpr std::size_t kMaxQueuedUnits = 120;
constexpr int kMessagesToLog = 40;  // camera messages logged per connection before its first video
constexpr std::chrono::microseconds kReplayFrameInterval{33'333};  // the live view's 30 fps
constexpr std::chrono::seconds kStreamRetry{1};  // the RTMP feed is not there (yet): try again

// Raises `maximum` to `value` if it is larger (lock-free, from any thread).
void raise_to(std::atomic<std::int64_t>& maximum, std::int64_t value) {
    for (auto current = maximum.load(); value > current && !maximum.compare_exchange_weak(current, value);) {
    }
}

}  // namespace

Pipeline::Pipeline(QObject* parent) : QObject(parent) {
    stats_timer_.setInterval(1000);
    connect(&stats_timer_, &QTimer::timeout, this, &Pipeline::report_stats);
}

Pipeline::~Pipeline() { stop(); }

void Pipeline::start(djivcam::SessionConfig config, djivcam::media::DecoderPreference decoder) {
    stop();
    start_decoder(decoder);
    auto on_video = [this](std::span<const std::uint8_t> bytes) { assembler_->push(bytes); };
    auto on_state = [this](djivcam::SessionState state, const std::string& detail) {
        video_since_connect_ = state == djivcam::SessionState::Streaming;
        if (state == djivcam::SessionState::WaitingForRoute) {
            messages_logged_ = 0;  // a new connection follows
        }
        if (state != djivcam::SessionState::Streaming) {
            assembler_->reset();
            holding_ = true;  // the stream restarts mid-GOP: wait for a keyframe
        } else if (camera_) {
            camera_->on_streaming();  // (re)subscribe to the camera's settings
        }
        emit stateChanged(QString::fromUtf8(djivcam::to_string(state)), QString::fromStdString(detail));
    };
    session_ = std::make_unique<djivcam::LiveViewSession>(std::move(config), on_video, on_state);
    camera_notified_ = false;
    camera_ = std::make_unique<djivcam::camera::CameraController>(
        *session_,
        [this] {
            if (!camera_notified_.exchange(true)) {
                emit cameraChanged();
            }
        },
        [this](const std::string& error) { emit cameraError(QString::fromStdString(error)); });
    session_->set_message_callback([this](const djivcam::duml::Frame& frame) {
        // Diagnostics: what the camera says before the first video of a connection (its answers to
        // the live-view start, its state pushes) goes to the log, a limited number per connection.
        if (!video_since_connect_ && messages_logged_ < kMessagesToLog) {
            ++messages_logged_;
            qInfo("camera before video: %s", frame.describe().c_str());
        }
        camera_->on_message(frame);
    });
    session_->set_gap_callback([this] { gap_pending_ = true; });
    session_->set_log_callback([](const std::string& line) { qInfo("session: %s", line.c_str()); });
    session_->start();
}

void Pipeline::setConnectAllowed(bool allowed) {
    if (session_) {
        session_->set_connect_allowed(allowed);
    }
}

void Pipeline::startReplay(const QString& path, djivcam::media::DecoderPreference decoder) {
    stop();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Cannot open %1").arg(path));
        return;
    }
    const QByteArray stream = file.readAll();
    auto units = std::make_shared<std::vector<djivcam::h264::AccessUnit>>();
    djivcam::h264::AccessUnitAssembler splitter([&units](djivcam::h264::AccessUnit&& unit) { units->push_back(std::move(unit)); });
    splitter.push({reinterpret_cast<const std::uint8_t*>(stream.constData()), static_cast<std::size_t>(stream.size())});
    splitter.finish();
    if (units->empty()) {
        emit errorOccurred(tr("No H.264 video in %1").arg(path));
        return;
    }
    start_decoder(decoder);
    replay_ = std::jthread([this, units](std::stop_token stop) { replay_loop(stop, *units); });
    emit stateChanged(QStringLiteral("streaming"), tr("replay of %1").arg(QFileInfo(path).fileName()));
}

void Pipeline::startStream(const QString& url, djivcam::media::DecoderPreference decoder) {
    stop();
    start_decoder(decoder);
    stream_ = std::jthread([this, target = url.toStdString()](std::stop_token stop) { stream_loop(stop, target); });
}

void Pipeline::stream_loop(std::stop_token stop, const std::string& url) {
    std::mutex sleep_mutex;
    std::condition_variable_any sleeper;
    while (!stop.stop_requested()) {
        emit stateChanged(QStringLiteral("connecting"), tr("RTMP feed"));
        bool streaming = false;
        const std::string ended = djivcam::media::NetworkStream::run(
            url,
            [&](djivcam::h264::AccessUnit&& unit) {
                if (!streaming) {
                    streaming = true;
                    emit stateChanged(QStringLiteral("streaming"), tr("RTMP feed"));
                }
                enqueue(std::move(unit));
            },
            stop);
        if (stop.stop_requested()) {
            return;
        }
        holding_ = true;  // the stream restarts mid-GOP: wait for a keyframe
        emit stateChanged(QStringLiteral("waiting for the RTMP feed"), QString::fromStdString(ended));
        std::unique_lock lock(sleep_mutex);
        sleeper.wait_for(lock, stop, kStreamRetry, [] { return false; });
    }
}

void Pipeline::start_decoder(djivcam::media::DecoderPreference decoder) {
    assembler_ = std::make_unique<djivcam::h264::AccessUnitAssembler>(
        [this](djivcam::h264::AccessUnit&& unit) { enqueue(std::move(unit)); });
    decoder_ = std::jthread([this, decoder](std::stop_token stop) { decode_loop(stop, decoder); });
    decoded_frames_ = 0;
    held_frames_ = 0;
    last_held_ = 0;
    gap_pending_ = false;
    holding_ = true;
    stream_bytes_ = 0;
    worst_delay_us_ = 0;
    width_ = height_ = 0;
    last_frames_ = 0;
    last_stream_bytes_ = 0;
    last_video_datagrams_ = 0;
    last_lost_ = 0;
    stats_timer_.start();
}

void Pipeline::stop() {
    stats_timer_.stop();
    if (session_) {
        session_->stop();  // joins the session thread: no more callbacks after this
        camera_.reset();   // talks through the session: goes first
        session_.reset();
    }
    if (replay_.joinable()) {
        replay_.request_stop();
        replay_.join();
    }
    if (stream_.joinable()) {
        stream_.request_stop();  // also interrupts a blocked network read
        stream_.join();
    }
    if (decoder_.joinable()) {
        decoder_.request_stop();
        wake_.notify_all();
        decoder_.join();
    }
    {
        std::lock_guard lock(mutex_);
        queue_.clear();
        assembler_.reset();
    }
    std::lock_guard lock(frame_mutex_);
    latest_frame_.reset();  // a frame decoded while stopping must not reappear after "No video"
}

void Pipeline::enqueue(djivcam::h264::AccessUnit&& unit) {
    stream_bytes_ += unit.data.size();
    // A unit completed after lost video contains the gap (a lost start of this unit merges it into
    // the previous one): it and everything after it is damaged until an intact keyframe.
    if (gap_pending_.exchange(false)) {
        holding_ = true;
    } else if (holding_ && unit.keyframe) {
        holding_ = false;
    }
    const bool damaged = holding_;
    {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= kMaxQueuedUnits) {
            queue_.clear();  // hopelessly behind: resync on the next keyframe
            holding_ = true;
        }
        queue_.push_back({std::move(unit), Clock::now(), damaged});
    }
    wake_.notify_one();
}

void Pipeline::replay_loop(std::stop_token stop, const std::vector<djivcam::h264::AccessUnit>& units) {
    std::mutex sleep_mutex;
    std::condition_variable_any sleeper;
    auto next = Clock::now();
    while (!stop.stop_requested()) {
        for (const auto& unit : units) {
            next += kReplayFrameInterval;
            std::unique_lock lock(sleep_mutex);
            sleeper.wait_until(lock, stop, next, [] { return false; });  // sleeps until `next` or stop
            if (stop.stop_requested()) {
                return;
            }
            auto copy = unit;
            enqueue(std::move(copy));
        }
    }
}

void Pipeline::decode_loop(std::stop_token stop, djivcam::media::DecoderPreference preference) {
    std::unique_ptr<djivcam::media::H264Decoder> decoder;
    try {
        decoder = std::make_unique<djivcam::media::H264Decoder>(preference);
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return;
    }
    emit decoderChanged(QString::fromStdString(decoder->backend()), QString::fromStdString(decoder->gpu()), decoder->hardware());
    Frame last_intact;  // shown while damaged frames are held back
#ifdef DJIVCAM_HAVE_VCAM
    std::optional<djivcam::media::Nv12Canvas> canvas;  // virtual camera frames, created on first use
#endif

    while (!stop.stop_requested()) {
        QueuedUnit queued;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, stop, [this] { return !queue_.empty(); })) {
                return;
            }
            queued = std::move(queue_.front());
            queue_.pop_front();
        }
        if (auto decoded = decoder->decode(queued.unit.data)) {
            const Frame frame = std::make_shared<const djivcam::media::Nv12Frame>(std::move(*decoded));
            ++decoded_frames_;
            // Damaged frames are decoded (the decoder needs them) but the last intact one stays up.
            const bool show = !(queued.damaged && hold_on_loss_);
            if (!show) {
                ++held_frames_;
            } else {
                last_intact = frame;
            }
            if (frame->width != width_ || frame->height != height_) {
                width_ = frame->width;
                height_ = frame->height;
                emit formatChanged(width_, height_);
            }
#ifdef DJIVCAM_HAVE_VCAM
            if (virtual_camera_ && virtual_camera_->running()) {
                if (!canvas) {
                    canvas.emplace(djivcam::vcam::kWidth, djivcam::vcam::kHeight);
                }
                if (last_intact) {
                    virtual_camera_->publish(canvas->draw(*last_intact));  // repeated while holding
                }
            }
#endif
            if (!show) {
                continue;
            }
            bool notify = false;
            {
                std::lock_guard lock(frame_mutex_);
                latest_frame_ = frame;  // shared with the UI thread, never modified again
                notify = !frame_notified_;
                frame_notified_ = true;
            }
            if (notify) {
                emit frameAvailable();
            }
            raise_to(worst_delay_us_, std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - queued.arrived).count());
        }
    }
}

djivcam::camera::CameraState Pipeline::takeCameraState() {
    camera_notified_ = false;
    return camera_ ? camera_->state() : djivcam::camera::CameraState{};
}

Pipeline::Frame Pipeline::takeLatestFrame() {
    std::lock_guard lock(frame_mutex_);
    frame_notified_ = false;
    return std::exchange(latest_frame_, nullptr);
}

void Pipeline::report_stats() {
    LiveStats out;
    const std::uint64_t frames = decoded_frames_;
    const std::uint64_t held = held_frames_;
    out.held = static_cast<double>(held - last_held_);
    last_held_ = held;
    const std::uint64_t bytes = stream_bytes_;
    out.fps = static_cast<double>(frames - last_frames_);
    out.kbps = static_cast<double>(bytes - last_stream_bytes_) * 8.0 / 1000.0;
    out.delay_ms = static_cast<double>(worst_delay_us_.exchange(0)) / 1000.0;
    last_frames_ = frames;
    last_stream_bytes_ = bytes;
    if (session_) {
        const auto stats = session_->stats();
        const auto received = stats.video_datagrams - last_video_datagrams_;
        const auto lost = stats.lost - last_lost_;
        out.loss_percent = received + lost ? 100.0 * static_cast<double>(lost) / static_cast<double>(received + lost) : 0.0;
        out.recovered = stats.recovered;
        out.duplicates = stats.duplicates;
        out.reconnects = stats.reconnects;
        last_video_datagrams_ = stats.video_datagrams;
        last_lost_ = stats.lost;
    }
    emit statsUpdated(out);
}
