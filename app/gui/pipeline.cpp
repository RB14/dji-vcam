#include "pipeline.h"

#include <exception>
#include <utility>

namespace {

// Access units are never dropped (every P-frame depends on the previous one); this only guards
// against unbounded growth if decoding ever stalls.
constexpr std::size_t kMaxQueuedUnits = 120;

}  // namespace

Pipeline::Pipeline(QObject* parent) : QObject(parent) {
    stats_timer_.setInterval(1000);
    connect(&stats_timer_, &QTimer::timeout, this, &Pipeline::report_stats);
}

Pipeline::~Pipeline() { stop(); }

void Pipeline::start(djivcam::SessionConfig config, djivcam::media::DecoderPreference decoder) {
    stop();
    assembler_ = std::make_unique<djivcam::h264::AccessUnitAssembler>(
        [this](djivcam::h264::AccessUnit&& unit) { enqueue(std::move(unit)); });
    decoder_ = std::jthread([this, decoder](std::stop_token stop) { decode_loop(stop, decoder); });

    auto on_video = [this](std::span<const std::uint8_t> bytes) { assembler_->push(bytes); };
    auto on_state = [this](djivcam::SessionState state, const std::string& detail) {
        if (state != djivcam::SessionState::Streaming) {
            assembler_->reset();
        }
        emit stateChanged(QString::fromUtf8(djivcam::to_string(state)), QString::fromStdString(detail));
    };
    session_ = std::make_unique<djivcam::LiveViewSession>(std::move(config), on_video, on_state);
    session_->start();
    decoded_frames_ = 0;
    last_frames_ = 0;
    last_video_bytes_ = 0;
    last_video_datagrams_ = 0;
    last_lost_ = 0;
    stats_timer_.start();
}

void Pipeline::stop() {
    stats_timer_.stop();
    if (session_) {
        session_->stop();  // joins the session thread: no more callbacks after this
        session_.reset();
    }
    if (decoder_.joinable()) {
        decoder_.request_stop();
        wake_.notify_all();
        decoder_.join();
    }
    std::lock_guard lock(mutex_);
    queue_.clear();
    assembler_.reset();
}

void Pipeline::enqueue(djivcam::h264::AccessUnit&& unit) {
    {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= kMaxQueuedUnits) {
            queue_.clear();  // hopelessly behind: resync on the next keyframe
        }
        queue_.push_back(std::move(unit));
    }
    wake_.notify_one();
}

void Pipeline::decode_loop(std::stop_token stop, djivcam::media::DecoderPreference preference) {
    std::unique_ptr<djivcam::media::H264Decoder> decoder;
    try {
        decoder = std::make_unique<djivcam::media::H264Decoder>(preference);
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return;
    }
    emit decoderChanged(QString::fromStdString(decoder->backend()), decoder->hardware());

    while (!stop.stop_requested()) {
        djivcam::h264::AccessUnit unit;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, stop, [this] { return !queue_.empty(); })) {
                return;
            }
            unit = std::move(queue_.front());
            queue_.pop_front();
        }
        if (auto frame = decoder->decode(unit.data)) {
            ++decoded_frames_;
            QImage image(frame->pixels.data(), frame->width, frame->height, frame->stride, QImage::Format_RGB32);
            bool notify = false;
            {
                std::lock_guard lock(frame_mutex_);
                latest_frame_ = image.copy();  // copy: the frame buffer dies with this scope
                notify = !frame_notified_;
                frame_notified_ = true;
            }
            if (notify) {
                emit frameAvailable();
            }
        }
    }
}

QImage Pipeline::takeLatestFrame() {
    std::lock_guard lock(frame_mutex_);
    frame_notified_ = false;
    return std::exchange(latest_frame_, QImage());
}

void Pipeline::report_stats() {
    if (!session_) {
        return;
    }
    const auto stats = session_->stats();
    const std::uint64_t frames = decoded_frames_;
    const double fps = static_cast<double>(frames - last_frames_);
    const double kbps = static_cast<double>(stats.video_bytes - last_video_bytes_) * 8.0 / 1000.0;
    const auto received = stats.video_datagrams - last_video_datagrams_;
    const auto lost = stats.lost - last_lost_;
    const double loss_percent = received + lost ? 100.0 * static_cast<double>(lost) / static_cast<double>(received + lost) : 0.0;
    last_frames_ = frames;
    last_video_bytes_ = stats.video_bytes;
    last_video_datagrams_ = stats.video_datagrams;
    last_lost_ = stats.lost;
    emit statsUpdated(fps, kbps, loss_percent, stats.reconnects);
}
