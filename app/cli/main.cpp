// dji-vcam-cli: runs the camera connection without the GUI and prints its progress.
//
// Usage: dji-vcam-cli [--ble] [--seconds N] [--identifier-file PATH] [--dump PATH]
//        dji-vcam-cli --vcam-test N    publish a test pattern to the DJI VCam webcam for N seconds
//        dji-vcam-cli --list-cameras   list the cameras apps can see
//        dji-vcam-cli --vcam-register  register the DJI VCam webcam for all users, for good
//                                      (administrator; the installer runs it)
//        dji-vcam-cli --vcam-unregister  remove it (all users, and this user's portable copy)
//        dji-vcam-cli --ble-scan N     list the Bluetooth LE devices advertising nearby for N seconds
//        dji-vcam-cli --decode-bench FILE [--decoder auto|gpu|cpu] [--frames-out FILE.nv12]
//                                      time each per-frame step of the live view on a recorded stream
//   --ble              wake the camera's Wi-Fi over Bluetooth first, then hang up, as DJI Mimo does
//   --ble-hold         with --ble: keep the Bluetooth link open (the app's behaviour before
//                      2026-09-25; the camera then sends no video after a short power-off)
//   --identifier-file  file holding the approved pairing identifier (never printed)
//   --dump             write the received H.264 stream to PATH
//   --send R,S,I[,HEX] once streaming, send a DUML request (receiver, command set, command id and
//                      payload in hex, e.g. 01,02,8e,0100) and print the reply; repeatable
//   --ble-release-test with --ble: after waking the camera, end the Bluetooth link inside this still
//                      running process and time how long until the camera advertises again (it only
//                      advertises while nobody is connected)
//   --no-answer        do not answer the camera's own requests on the datalink (experiment)
//   --ble-answer       with --ble: answer the camera's own requests over Bluetooth (DJI Mimo does not)
//   --ble-send R,S,I[,HEX]  with --ble: after waking the camera, send a DUML request over Bluetooth
//                      (same format as --send) and print the reply; repeatable
//   --send-early       send the --send requests right after the live-view trigger instead, before
//                      any video (e.g. to try to start the video of a camera that sends none)
//   --show-messages    print every DUML message the camera sends (status pushes)
//   --camera           follow the camera's settings and status (subscriptions) and print them
//   --camera-set N=C   once streaming, change setting N (e.g. Stabilization, EV) to code C through
//                      the camera controls, confirming on the camera's read-back; repeatable
//   --gap-wait MS      wait up to MS for a missing video datagram (re-sent by the camera) before
//                      skipping it; 0 (default) acknowledges the newest datagram at once
//   --camera-ip IP     the camera's address (default 192.168.2.1), e.g. 127.0.0.1 for
//                      tools/fake_camera.py
//   --reconnect-test S[,S...]  after streaming, drop the session (no goodbye), stay silent S
//                      seconds, connect again and report whether video comes back; per value
#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "djivcam/camera_controller.h"
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
    std::string error;
    if (!vc::VirtualCamera::camera_registered() && !vc::VirtualCamera::register_camera(false, &error)) {
        say("virtual camera registration failed: " + error);
        return 1;
    }
    vc::VirtualCamera camera;
    camera.start();
    say("publishing a test pattern: open \"DJI VCam\" in an app (Windows Camera, OBS, browser)");
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
// With `frames_out`, also writes every decoded frame there as raw NV12 (to compare decoders).
int run_decode_bench(const std::string& path, djivcam::media::DecoderPreference preference, const std::string& frames_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        say("cannot open " + path);
        return 1;
    }
    const std::vector<std::uint8_t> stream((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<djivcam::h264::AccessUnit> units;
    djivcam::h264::AccessUnitAssembler assembler([&](djivcam::h264::AccessUnit&& unit) { units.push_back(std::move(unit)); });
    assembler.push(stream);
    assembler.finish();

    djivcam::media::H264Decoder decoder(preference);
    say("decoder: " + decoder.backend() + ", " + std::to_string(units.size()) + " access units");
    djivcam::media::Nv12Canvas canvas(1280, 720);
    StepTimer decode("decode + download (NV12)"), webcam("webcam NV12 canvas");
    std::ofstream out;
    if (!frames_out.empty()) {
        out.open(frames_out, std::ios::binary);
    }
    int frames = 0;
    for (const auto& unit : units) {
        auto frame = decode.measure([&] { return decoder.decode(unit.data); });
        if (!frame) {
            continue;
        }
        ++frames;
        if (out) {
            out.write(reinterpret_cast<const char*>(frame->data.data()), static_cast<std::streamsize>(frame->data.size()));
        }
        webcam.measure([&] { return canvas.draw(*frame); });
    }
    say(std::to_string(frames) + " frames");
    decode.report();
    webcam.report();
    const double per_frame = decode.mean() + webcam.mean();
    char line[120];
    std::snprintf(line, sizeof(line), "total %.2f ms per frame: up to %.0f fps on the decode thread", per_frame, 1000.0 / per_frame);
    say(line);
    return 0;
}

}  // namespace
#endif

namespace {

// A DUML request given as "receiver,cmd_set,cmd_id[,payload]", all in hex.
struct SendSpec {
    std::uint8_t receiver = 0;
    std::uint8_t cmd_set = 0;
    std::uint8_t cmd_id = 0;
    djivcam::duml::Bytes payload;
};

std::optional<SendSpec> parse_send(const std::string& text) {
    std::vector<std::string> fields(1);
    for (char c : text) {
        if (c == ',') {
            fields.emplace_back();
        } else {
            fields.back() += c;
        }
    }
    if (fields.size() < 3 || fields.size() > 4) {
        return std::nullopt;
    }
    try {
        SendSpec spec{static_cast<std::uint8_t>(std::stoul(fields[0], nullptr, 16)),
                      static_cast<std::uint8_t>(std::stoul(fields[1], nullptr, 16)),
                      static_cast<std::uint8_t>(std::stoul(fields[2], nullptr, 16)),
                      {}};
        if (fields.size() == 4) {
            const std::string& hex = fields[3];
            if (hex.size() % 2 != 0) {
                return std::nullopt;
            }
            for (std::size_t i = 0; i < hex.size(); i += 2) {
                spec.payload.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            }
        }
        return spec;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// One line with everything the camera has reported.
std::string summarize(const djivcam::camera::CameraState& state) {
    using djivcam::camera::Setting;
    std::string out;
    auto add = [&out](const std::string& part) { out += (out.empty() ? "" : " | ") + part; };
    for (const auto& [setting, code] : state.values) {
        add(std::string(djivcam::camera::name(setting)) + " " + djivcam::camera::describe(setting, code));
    }
    if (state.resolution && state.frame_rate) {
        add(djivcam::camera::describe_format(*state.resolution, *state.frame_rate));
    }
    if (state.white_balance_kelvin) {
        add(*state.white_balance_kelvin ? "WB " + std::to_string(*state.white_balance_kelvin) + " K" : "WB auto");
    }
    if (state.shutter_actual) {
        add("shutter 1/" + std::to_string(*state.shutter_actual));
    }
    if (state.battery_percent) {
        add("battery " + std::to_string(*state.battery_percent) + "%");
    }
    if (state.storage) {
        add("free " + std::to_string(state.storage->free_mb) + " MB");
    }
    if (state.recording) {
        add("REC " + std::to_string(state.record_seconds.value_or(0)) + " s");
    }
    if (!state.allowed_formats.empty()) {
        add(std::to_string(state.allowed_formats.size()) + " formats allowed");
    }
    return out.empty() ? "(nothing reported yet)" : out;
}

// "Stabilization=3" -> (setting, code).
std::optional<std::pair<djivcam::camera::Setting, int>> parse_camera_set(const std::string& text) {
    const auto equals = text.find('=');
    if (equals == std::string::npos) {
        return std::nullopt;
    }
    std::string wanted = text.substr(0, equals);
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (int value = 0; value <= static_cast<int>(djivcam::camera::Setting::Codec); ++value) {
        const auto setting = static_cast<djivcam::camera::Setting>(value);
        std::string name(djivcam::camera::name(setting));
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == wanted) {
            try {
                return std::pair(setting, std::stoi(text.substr(equals + 1), nullptr, 0));
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

// Replies to the session's own keep-alive traffic (heartbeat, registration, live-view trigger).
bool is_session_housekeeping(const djivcam::duml::Frame& frame) {
    return frame.cmd_set == 0x00 && (frame.cmd_id == 0x4F || frame.cmd_id == 0x81 || frame.cmd_id == 0x82 || frame.cmd_id == 0x88);
}

}  // namespace

int main(int argc, char* argv[]) {
    int seconds = 20;
    bool use_ble = false;
    int vcam_test_seconds = 0;
    bool vcam_register = false;
    bool list_cameras = false;
    bool vcam_unregister = false;
    int ble_scan_seconds = 0;
    std::string bench_file;
    std::string decoder_choice = "auto";
    std::string frames_out;
    std::string identifier_file;
    std::string dump_path;
    std::vector<SendSpec> sends;
    std::vector<SendSpec> ble_sends;
    bool show_messages = false;
    bool send_early = false;
    bool no_answer = false;
    [[maybe_unused]] bool ble_answer = false;
    [[maybe_unused]] bool ble_hold = false;
    [[maybe_unused]] bool ble_release_test = false;
    bool show_all_messages = false;  // including the replies to the session's keep-alives
    bool follow_camera = false;
    std::vector<std::pair<djivcam::camera::Setting, int>> camera_sets;
    std::string camera_ip;
    int gap_wait_ms = 0;
    std::vector<int> reconnect_gaps;  // seconds of silence before each reconnect
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = i + 1 < argc;
        if (flag == "--ble") {
            use_ble = true;
        } else if (flag == "--ble-scan" && has_value) {
            ble_scan_seconds = std::stoi(argv[++i]);
        } else if (flag == "--list-cameras") {
            list_cameras = true;
        } else if (flag == "--vcam-register") {
            vcam_register = true;
        } else if (flag == "--vcam-unregister") {
            vcam_unregister = true;
        } else if (flag == "--vcam-test" && has_value) {
            vcam_test_seconds = std::stoi(argv[++i]);
        } else if (flag == "--decode-bench" && has_value) {
            bench_file = argv[++i];
        } else if (flag == "--decoder" && has_value) {
            decoder_choice = argv[++i];
        } else if (flag == "--frames-out" && has_value) {
            frames_out = argv[++i];
        } else if (flag == "--seconds" && has_value) {
            seconds = std::stoi(argv[++i]);
        } else if (flag == "--identifier-file" && has_value) {
            identifier_file = argv[++i];
        } else if (flag == "--dump" && has_value) {
            dump_path = argv[++i];
        } else if (flag == "--send" && has_value) {
            const auto spec = parse_send(argv[++i]);
            if (!spec) {
                std::fprintf(stderr, "bad --send value: %s (expected e.g. 01,02,8e,0100)\n", argv[i]);
                return 2;
            }
            sends.push_back(*spec);
        } else if (flag == "--ble-release-test") {
            ble_release_test = true;
        } else if (flag == "--ble-send" && has_value) {
            const auto spec = parse_send(argv[++i]);
            if (!spec) {
                std::fprintf(stderr, "bad --ble-send value: %s (expected e.g. 07,07,15)\n", argv[i]);
                return 2;
            }
            ble_sends.push_back(*spec);
        } else if (flag == "--no-answer") {
            no_answer = true;
        } else if (flag == "--ble-hold") {
            ble_hold = true;
        } else if (flag == "--ble-answer") {
            ble_answer = true;
        } else if (flag == "--send-early") {
            send_early = true;
        } else if (flag == "--show-messages") {
            show_messages = true;
        } else if (flag == "--show-all-messages") {
            show_messages = show_all_messages = true;
        } else if (flag == "--camera") {
            follow_camera = true;
        } else if (flag == "--camera-set" && has_value) {
            const auto change = parse_camera_set(argv[++i]);
            if (!change) {
                std::fprintf(stderr, "bad --camera-set value: %s (expected e.g. Stabilization=3)\n", argv[i]);
                return 2;
            }
            camera_sets.push_back(*change);
            follow_camera = true;
        } else if (flag == "--gap-wait" && has_value) {
            gap_wait_ms = std::stoi(argv[++i]);
        } else if (flag == "--camera-ip" && has_value) {
            camera_ip = argv[++i];
        } else if (flag == "--reconnect-test" && has_value) {
            const std::string list = argv[++i];
            for (std::size_t start = 0; start <= list.size();) {
                const std::size_t end = std::min(list.find(',', start), list.size());
                reconnect_gaps.push_back(std::stoi(list.substr(start, end - start)));
                start = end + 1;
            }
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", flag.c_str());
            return 2;
        }
    }

    if (list_cameras) {
#ifdef DJIVCAM_HAVE_VCAM
        for (const std::wstring& name : djivcam::vcam::VirtualCamera::list_cameras()) {
            std::printf("%ls\n", name.c_str());
        }
        return 0;
#else
        std::fprintf(stderr, "built without the virtual camera\n");
        return 2;
#endif
    }

    if (vcam_register || vcam_unregister) {
#ifdef DJIVCAM_HAVE_VCAM
        // Run by the installer (elevated): the webcam for all users, for good; and its removal. The
        // removal also takes a copy a portable run registered for this user.
        namespace vc = djivcam::vcam;
        std::string error;
        if (vcam_register) {
            std::string ignored;
            vc::VirtualCamera::unregister_camera(false, &ignored);  // a portable copy's: no duplicate entry
            if (!vc::VirtualCamera::register_camera(true, &error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            say("DJI VCam webcam registered for all users");
            return 0;
        }
        const bool all = vc::VirtualCamera::unregister_camera(true, &error);
        std::string user_error;
        const bool user = vc::VirtualCamera::unregister_camera(false, &user_error);
        say(std::string("DJI VCam webcam removed: all users ") + (all ? "yes" : "no (" + error + ")") + ", this user " +
            (user ? "yes" : "no (" + user_error + ")"));
        return 0;
#else
        std::fprintf(stderr, "built without the virtual camera\n");
        return 2;
#endif
    }

    if (vcam_test_seconds > 0) {
#ifdef DJIVCAM_HAVE_VCAM
        return run_vcam_test(vcam_test_seconds);
#else
        std::fprintf(stderr, "built without the virtual camera\n");
        return 2;
#endif
    }

    if (ble_scan_seconds > 0) {
#ifdef DJIVCAM_HAVE_BLE
        say("scanning for Bluetooth LE devices for " + std::to_string(ble_scan_seconds) + " s");
        const auto devices = djivcam::ble::scan_nearby(std::chrono::seconds(ble_scan_seconds));
        for (const auto& device : devices) {
            char dji[32] = "";
            if (device.dji_model) {
                std::snprintf(dji, sizeof(dji), "  DJI, model 0x%02x", *device.dji_model);
            }
            char line[160];
            std::snprintf(line, sizeof(line), "%s  %4d dBm  %-24s%s", device.address.c_str(), device.rssi,
                          device.name.empty() ? "(no name)" : device.name.c_str(), dji);
            say(line);
        }
        say(std::to_string(devices.size()) + " devices");
        return 0;
#else
        std::fprintf(stderr, "built without Bluetooth support\n");
        return 2;
#endif
    }

    if (!bench_file.empty()) {
#ifdef DJIVCAM_HAVE_MEDIA
        using djivcam::media::DecoderPreference;
        return run_decode_bench(bench_file, decoder_choice == "gpu"   ? DecoderPreference::Hardware
                                            : decoder_choice == "cpu" ? DecoderPreference::Software
                                                                      : DecoderPreference::Auto,
                                frames_out);
#else
        std::fprintf(stderr, "built without the video decoder\n");
        return 2;
#endif
    }

    djivcam::SessionConfig config;
    config.gap_timeout = std::chrono::milliseconds(gap_wait_ms);
    config.answer_requests = !no_answer;
    if (!camera_ip.empty()) {
        config.camera_ip = camera_ip;
        config.camera_subnet_prefix = camera_ip.substr(0, camera_ip.rfind('.') + 1);
    }
    if (!identifier_file.empty()) {
        std::ifstream in(identifier_file);
        std::getline(in, config.identifier);
    }

#ifdef DJIVCAM_HAVE_BLE
    std::optional<djivcam::ble::CameraBle> camera;
    std::jthread keepalive;
    if (use_ble) {
        camera.emplace([](const std::string& message) { say("ble: " + message); });
        camera->set_answer_requests(ble_answer);
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
        for (const SendSpec& spec : ble_sends) {
            const std::string what = djivcam::duml::Frame{djivcam::duml::kAddrApp, spec.receiver, 0, djivcam::duml::kFlagRequest,
                                                          spec.cmd_set, spec.cmd_id, spec.payload}
                                         .describe();
            const auto reply = camera->request(spec.receiver, spec.cmd_set, spec.cmd_id, spec.payload, 3s);
            say("bluetooth sent " + what + " -> " + (reply ? reply->describe() : std::string("no reply")));
        }
        if (ble_release_test) {
            camera->disconnect();
            const auto released = steady_clock::now();
            say("released the Bluetooth link; this process keeps running");
            djivcam::ble::CameraBle watcher;
            while (steady_clock::now() - released < 60s) {
                if (watcher.find_camera(3s, found->address)) {
                    say("the camera advertises again " +
                        std::to_string(duration_cast<milliseconds>(steady_clock::now() - released).count()) +
                        " ms after the release");
                    return 0;
                }
            }
            say("the camera did not advertise within 60 s: the link is still held");
            return 1;
        }
        if (ble_hold) {
            keepalive = std::jthread([&camera](std::stop_token stop) {
                while (!stop.stop_requested()) {
                    camera->keepalive();
                    std::this_thread::sleep_for(1s);
                }
            });
        } else {
            camera->disconnect();
            say("hung up Bluetooth (the datalink runs without it, as with DJI Mimo)");
        }
    }
#else
    if (use_ble) {
        std::fprintf(stderr, "built without Bluetooth support\n");
        return 2;
    }
#endif

    std::unique_ptr<djivcam::camera::CameraController> controls;  // before the session: its callbacks use it
    std::ofstream dump;
    if (!dump_path.empty()) {
        dump.open(dump_path, std::ios::binary);
    }
    std::atomic<bool> waiting_for_video{false};  // after the live-view trigger, before any video
    djivcam::LiveViewSession session(
        config,
        [&](std::span<const std::uint8_t> video) {
            if (dump) {
                dump.write(reinterpret_cast<const char*>(video.data()), static_cast<std::streamsize>(video.size()));
            }
        },
        [&controls, &waiting_for_video](djivcam::SessionState state, const std::string& detail) {
            say(std::string("state: ") + djivcam::to_string(state) + (detail.empty() ? "" : " - " + detail));
            waiting_for_video = state == djivcam::SessionState::Connecting && detail == "waiting for video";
            if (state == djivcam::SessionState::Streaming && controls) {
                controls->on_streaming();
            }
        });
    std::atomic<bool> camera_changed{false};
    if (follow_camera) {
        controls = std::make_unique<djivcam::camera::CameraController>(
            session, [&camera_changed] { camera_changed = true; },
            [](const std::string& error) { say("camera error: " + error); });
    }
    session.set_message_callback([&controls, show_messages, show_all_messages](const djivcam::duml::Frame& frame) {
        if (show_messages && (show_all_messages || !is_session_housekeeping(frame))) {
            say("camera: " + frame.describe());
        }
        if (controls) {
            controls->on_message(frame);
        }
    });
    session.set_log_callback([](const std::string& line) { say("session: " + line); });
    session.start();
    djivcam::SessionStats previous{};
    bool sent = false;
    for (int second = 0; second < seconds; ++second) {
        std::this_thread::sleep_for(1s);
        const bool ready = send_early ? waiting_for_video.load() : session.state() == djivcam::SessionState::Streaming;
        if (!sent && ready) {
            sent = true;
            for (const SendSpec& spec : sends) {
                const std::string what = djivcam::duml::Frame{djivcam::duml::kAddrApp, spec.receiver, 0, djivcam::duml::kFlagRequest,
                                                              spec.cmd_set, spec.cmd_id, spec.payload}
                                             .describe();
                session.request(spec.receiver, spec.cmd_set, spec.cmd_id, spec.payload,
                                [what](std::optional<djivcam::duml::Frame> reply) {
                                    say("sent " + what + " -> " + (reply ? reply->describe() : std::string("no reply")));
                                });
                std::this_thread::sleep_for(300ms);
            }
            for (const auto& [setting, code] : camera_sets) {
                say("camera: set " + std::string(djivcam::camera::name(setting)) + " to " + djivcam::camera::describe(setting, code));
                controls->set(setting, code);
            }
        }
        if (controls && camera_changed.exchange(false)) {
            say("camera: " + summarize(controls->state()));
        }
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
    for (const int gap : reconnect_gaps) {
        session.stop();
        say("reconnect test: session dropped, silent for " + std::to_string(gap) + " s");
        std::this_thread::sleep_for(std::chrono::seconds(gap));
        session.start();
        const auto started = steady_clock::now();
        while (session.state() != djivcam::SessionState::Streaming && steady_clock::now() - started < 20s) {
            std::this_thread::sleep_for(100ms);
        }
        const bool video = session.state() == djivcam::SessionState::Streaming;
        say("reconnect test: after " + std::to_string(gap) + " s of silence: " +
            (video ? "video after " + std::to_string(duration_cast<milliseconds>(steady_clock::now() - started).count()) + " ms"
                   : std::string("NO VIDEO within 20 s")));
        std::this_thread::sleep_for(3s);
    }
    session.stop();
    return 0;
}
