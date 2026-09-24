// dji-vcam-cli: runs the camera connection without the GUI and prints its progress.
//
// Usage: dji-vcam-cli [--ble] [--seconds N] [--identifier-file PATH] [--dump PATH]
//        dji-vcam-cli --vcam-test N    publish a test pattern to the DJI VCam webcam for N seconds
//        dji-vcam-cli --decode-bench FILE [--decoder auto|gpu|cpu]
//                                      time each per-frame step of the live view on a recorded stream
//   --ble              wake the camera's Wi-Fi over Bluetooth first and keep the BLE link alive
//   --identifier-file  file holding the approved pairing identifier (never printed)
//   --dump             write the received H.264 stream to PATH
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "djivcam/session.h"

#ifdef DJIVCAM_HAVE_BLE
#include "djivcam/camera_ble.h"
#endif

#ifdef DJIVCAM_HAVE_MEDIA
#include <iterator>
#include <numeric>

#include "djivcam/decoder.h"
#include "djivcam/h264.h"
#endif

#ifdef DJIVCAM_HAVE_VCAM
#include "djivcam/vcam_protocol.h"
#include "djivcam/virtual_camera.h"
#endif

namespace {

using namespace std::chrono;

const auto g_start = steady_clock::now();

void say(const std::string& message) {
    std::printf("[%7.3f] %s\n", duration_cast<milliseconds>(steady_clock::now() - g_start).count() / 1000.0,
                message.c_str());
    std::fflush(stdout);
}

}  // namespace

#ifdef DJIVCAM_HAVE_VCAM
namespace {

// Starts the "DJI VCam" webcam and publishes a moving test pattern for `seconds`.
int run_vcam_test(int seconds) {
    namespace vc = djivcam::vcam;
    say(std::string("virtual camera component registered: ") + (vc::VirtualCamera::source_registered() ? "yes" : "no"));
    vc::VirtualCamera camera;
    std::string error;
    if (!camera.start(&error)) {
        say("virtual camera start failed: " + error);
        return 1;
    }
    say("virtual camera started: open \"DJI VCam\" in an app (Windows Camera, OBS, browser)");
    std::vector<std::uint8_t> frame(vc::kFrameSize);
    const auto end = steady_clock::now() + std::chrono::seconds(seconds);
    for (int n = 0; steady_clock::now() < end; ++n) {
        for (std::uint32_t y = 0; y < vc::kHeight; ++y) {  // diagonal luma bars moving right
            for (std::uint32_t x = 0; x < vc::kWidth; ++x) {
                frame[y * vc::kWidth + x] = static_cast<std::uint8_t>(((x + y + n * 8) / 64 % 2) ? 200 : 40);
            }
        }
        std::fill(frame.begin() + vc::kWidth * vc::kHeight, frame.end(), std::uint8_t{128});
        camera.publish(frame.data());
        if (n % 30 == 0) {
            say(std::string("publishing, consumer attached: ") + (camera.in_use() ? "yes" : "no"));
        }
        std::this_thread::sleep_for(33ms);
    }
    camera.stop();
    return 0;
}

}  // namespace
#endif

#ifdef DJIVCAM_HAVE_MEDIA
namespace {

// Per-frame cost of one step, in milliseconds.
class StepTimer {
public:
    explicit StepTimer(std::string name) : name_(std::move(name)) {}
    template <typename F>
    auto measure(F&& step) {
        const auto begin = steady_clock::now();
        auto result = step();
        samples_.push_back(duration<double, std::milli>(steady_clock::now() - begin).count());
        return result;
    }
    double mean() const {
        return samples_.empty() ? 0 : std::accumulate(samples_.begin(), samples_.end(), 0.0) / static_cast<double>(samples_.size());
    }
    void report() {
        if (samples_.empty()) {
            return;
        }
        std::sort(samples_.begin(), samples_.end());
        char line[160];
        std::snprintf(line, sizeof(line), "%-26s mean %6.2f ms   p95 %6.2f ms   max %6.2f ms", name_.c_str(), mean(),
                      samples_[samples_.size() * 95 / 100], samples_.back());
        say(line);
    }

private:
    std::string name_;
    std::vector<double> samples_;
};

// Decodes a recorded Annex-B stream as fast as possible and reports what each per-frame step of
// the live view costs, i.e. how much headroom is left at 30 fps.
int run_decode_bench(const std::string& path, djivcam::media::DecoderPreference preference) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        say("cannot open " + path);
        return 1;
    }
    const std::vector<std::uint8_t> stream((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<djivcam::h264::AccessUnit> units;
    djivcam::h264::AccessUnitAssembler assembler([&](djivcam::h264::AccessUnit&& unit) { units.push_back(std::move(unit)); });
    assembler.push(stream);
    assembler.flush();

    djivcam::media::H264Decoder decoder(preference);
    say("decoder: " + decoder.backend() + ", " + std::to_string(units.size()) + " access units");
    djivcam::media::Nv12Canvas canvas(1280, 720);
    StepTimer decode("decode + download + BGRA"), webcam("webcam NV12 canvas"), preview("preview image copy");
    int frames = 0;
    for (const auto& unit : units) {
        auto frame = decode.measure([&] { return decoder.decode(unit.data); });
        if (!frame) {
            continue;
        }
        ++frames;
        webcam.measure([&] { return canvas.draw(*frame).size(); });
        preview.measure([&] { return std::vector<std::uint8_t>(frame->pixels).size(); });
    }
    say(std::to_string(frames) + " frames");
    decode.report();
    webcam.report();
    preview.report();
    const double per_frame = decode.mean() + webcam.mean() + preview.mean();
    char line[120];
    std::snprintf(line, sizeof(line), "total %.2f ms per frame: up to %.0f fps on the decode thread", per_frame, 1000.0 / per_frame);
    say(line);
    return 0;
}

}  // namespace
#endif

int main(int argc, char* argv[]) {
    int seconds = 20;
    bool use_ble = false;
    int vcam_test_seconds = 0;
    std::string bench_file;
    std::string decoder_choice = "auto";
    std::string identifier_file;
    std::string dump_path;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = i + 1 < argc;
        if (flag == "--ble") {
            use_ble = true;
        } else if (flag == "--vcam-test" && has_value) {
            vcam_test_seconds = std::stoi(argv[++i]);
        } else if (flag == "--decode-bench" && has_value) {
            bench_file = argv[++i];
        } else if (flag == "--decoder" && has_value) {
            decoder_choice = argv[++i];
        } else if (flag == "--seconds" && has_value) {
            seconds = std::stoi(argv[++i]);
        } else if (flag == "--identifier-file" && has_value) {
            identifier_file = argv[++i];
        } else if (flag == "--dump" && has_value) {
            dump_path = argv[++i];
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", flag.c_str());
            return 2;
        }
    }

    if (vcam_test_seconds > 0) {
#ifdef DJIVCAM_HAVE_VCAM
        return run_vcam_test(vcam_test_seconds);
#else
        std::fprintf(stderr, "built without the virtual camera\n");
        return 2;
#endif
    }

    if (!bench_file.empty()) {
#ifdef DJIVCAM_HAVE_MEDIA
        using djivcam::media::DecoderPreference;
        return run_decode_bench(bench_file, decoder_choice == "gpu"   ? DecoderPreference::Hardware
                                            : decoder_choice == "cpu" ? DecoderPreference::Software
                                                                      : DecoderPreference::Auto);
#else
        std::fprintf(stderr, "built without the video decoder\n");
        return 2;
#endif
    }

    djivcam::SessionConfig config;
    if (!identifier_file.empty()) {
        std::ifstream in(identifier_file);
        std::getline(in, config.identifier);
    }

#ifdef DJIVCAM_HAVE_BLE
    std::optional<djivcam::ble::CameraBle> camera;
    std::jthread keepalive;
    if (use_ble) {
        camera.emplace([](const std::string& message) { say("ble: " + message); });
        say("searching for the camera over Bluetooth (wake it up if it is asleep)");
        const auto found = camera->find_camera(60s);
        if (!found) {
            say("no DJI camera found");
            return 1;
        }
        say("found '" + found->name + "' " + found->address + " rssi " + std::to_string(found->rssi) + " model " +
            std::to_string(found->model));
        if (!camera->connect(*found)) {
            return 1;
        }
        const auto paired = camera->pair(config.identifier, config.token, 60s,
                                         [] { say(">>> approve the pairing prompt on the camera screen <<<"); });
        if (paired == djivcam::ble::PairResult::TimedOut || paired == djivcam::ble::PairResult::Failed) {
            say("pairing failed");
            return 1;
        }
        say(paired == djivcam::ble::PairResult::AlreadyPaired ? "already paired" : "pairing approved");
        const auto credentials = camera->wake_wifi();
        if (!credentials) {
            return 1;
        }
        say("camera Wi-Fi is up: '" + credentials->ssid + "' (password not shown)");
        keepalive = std::jthread([&camera](std::stop_token stop) {
            while (!stop.stop_requested()) {
                camera->keepalive();
                std::this_thread::sleep_for(1s);
            }
        });
    }
#else
    if (use_ble) {
        std::fprintf(stderr, "built without Bluetooth support\n");
        return 2;
    }
#endif

    std::ofstream dump;
    if (!dump_path.empty()) {
        dump.open(dump_path, std::ios::binary);
    }
    djivcam::LiveViewSession session(
        config,
        [&](std::span<const std::uint8_t> video) {
            if (dump) {
                dump.write(reinterpret_cast<const char*>(video.data()), static_cast<std::streamsize>(video.size()));
            }
        },
        [](djivcam::SessionState state, const std::string& detail) {
            say(std::string("state: ") + djivcam::to_string(state) + (detail.empty() ? "" : " - " + detail));
        });
    session.start();
    djivcam::SessionStats previous{};
    for (int second = 0; second < seconds; ++second) {
        std::this_thread::sleep_for(1s);
        const auto stats = session.stats();
        char line[200];
        std::snprintf(line, sizeof(line),
                      "datagrams +%llu, video +%llu (%.0f kbit/s), lost %llu, recovered %llu, dup %llu, reconnects %llu",
                      static_cast<unsigned long long>(stats.datagrams - previous.datagrams),
                      static_cast<unsigned long long>(stats.video_datagrams - previous.video_datagrams),
                      static_cast<double>(stats.video_bytes - previous.video_bytes) * 8 / 1000,
                      static_cast<unsigned long long>(stats.lost), static_cast<unsigned long long>(stats.recovered),
                      static_cast<unsigned long long>(stats.duplicates),
                      static_cast<unsigned long long>(stats.reconnects));
        say(line);
        previous = stats;
    }
    session.stop();
    return 0;
}
