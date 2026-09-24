#include "djivcam/camera_ble.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "central.h"
#include "djivcam/duml.h"

namespace djivcam::ble {
namespace {

using namespace std::chrono_literals;
using duml::Bytes;

constexpr detail::ShortUuid kService = 0xFFF0;
constexpr detail::ShortUuid kNotify = 0xFFF4;
constexpr detail::ShortUuid kWrite = 0xFFF5;

constexpr std::uint16_t kPairingSeq = 0x8092;
constexpr auto kWriteSpacing = 20ms;  // back-to-back write-without-response frames get dropped
constexpr auto kNameWait = 1500ms;     // how long a found camera may still send its name

// 62-byte "APP" device-info blob the camera expects in reply to its 0x00/0x81 request.
Bytes app_device_info() {
    Bytes blob(62, 0x00);
    blob[1] = 'A';
    blob[2] = 'P';
    blob[3] = 'P';
    blob[41] = 0x02;
    blob[50] = 0x02;
    blob[51] = 0x08;
    return blob;
}

// Replies to GetWifiSsid / GetWifiPassword are [status u8][len u8][ascii].
std::optional<std::string> parse_string_reply(const std::optional<duml::Frame>& frame) {
    if (!frame || frame->payload.size() < 2 || frame->payload[0] != 0) {
        return std::nullopt;
    }
    const std::size_t len = std::min<std::size_t>(frame->payload[1], frame->payload.size() - 2);
    return std::string(frame->payload.begin() + 2, frame->payload.begin() + 2 + static_cast<std::ptrdiff_t>(len));
}

}  // namespace

struct CameraBle::Impl {
    Log log;
    std::unique_ptr<detail::Central> central;
    std::unique_ptr<detail::GattLink> link;
    std::string scanned_address;  // the camera find_camera() returned

    std::mutex mutex;
    std::condition_variable_any changed;
    duml::StreamParser parser;
    std::vector<duml::Frame> responses;  // unclaimed responses from the camera
    bool approved = false;

    std::deque<Bytes> outbox;  // frames for the writer thread
    std::condition_variable_any outbox_ready;
    std::jthread writer;

    std::uint16_t next_seq = 0x8000;

    void say(const std::string& message) {
        if (log) {
            log(message);
        }
    }

    void start_writer() {
        writer = std::jthread([this](std::stop_token stop) {
            while (true) {
                Bytes frame;
                {
                    std::unique_lock lock(mutex);
                    if (!outbox_ready.wait(lock, stop, [this] { return !outbox.empty(); })) {
                        return;
                    }
                    frame = std::move(outbox.front());
                    outbox.pop_front();
                }
                if (link && !link->write(kService, kWrite, frame, false)) {
                    say("write failed");
                }
                std::this_thread::sleep_for(kWriteSpacing);
            }
        });
    }

    void send(const duml::Frame& frame) {
        {
            std::lock_guard lock(mutex);
            outbox.push_back(frame.encode());
        }
        outbox_ready.notify_one();
    }

    // Runs on the Bluetooth stack's thread: parse, answer camera requests, hand over responses.
    void on_notify(const Bytes& bytes) {
        std::vector<duml::Frame> frames;
        {
            std::lock_guard lock(mutex);
            frames = parser.feed(bytes);
        }
        for (duml::Frame& frame : frames) {
            if (frame.is_request()) {
                // Every camera request must be answered or the camera drops the link.
                if (frame.cmd_set == 0x07 && frame.cmd_id == 0x46) {
                    std::lock_guard lock(mutex);
                    approved = true;
                }
                const bool device_info = frame.cmd_set == 0x00 && frame.cmd_id == 0x81;
                send(frame.reply(device_info ? app_device_info() : frame.payload));
            } else {
                std::lock_guard lock(mutex);
                responses.push_back(std::move(frame));
            }
        }
        changed.notify_all();
    }

    std::optional<duml::Frame> request(std::uint8_t receiver, std::uint8_t cmd_set, std::uint8_t cmd_id, Bytes payload,
                                       std::chrono::milliseconds timeout, std::optional<std::uint16_t> seq = {}) {
        {
            std::lock_guard lock(mutex);
            std::erase_if(responses, [&](const duml::Frame& f) { return f.cmd_set == cmd_set && f.cmd_id == cmd_id; });
        }
        send(duml::Frame{duml::kAddrApp, receiver, seq.value_or(next_seq++), duml::kFlagRequest, cmd_set, cmd_id,
                         std::move(payload)});
        std::unique_lock lock(mutex);
        std::optional<duml::Frame> reply;
        changed.wait_for(lock, timeout, [&] {
            for (auto it = responses.begin(); it != responses.end(); ++it) {
                if (it->cmd_set == cmd_set && it->cmd_id == cmd_id) {
                    reply = std::move(*it);
                    responses.erase(it);
                    return true;
                }
            }
            return false;
        });
        return reply;
    }
};

CameraBle::CameraBle(Log log) : impl_(std::make_unique<Impl>()) {
    impl_->log = std::move(log);
    std::random_device random;
    impl_->next_seq = static_cast<std::uint16_t>(0x8100 + random() % 0x0E00);
}

CameraBle::~CameraBle() { disconnect(); }

bool CameraBle::bluetooth_available() { return detail::Central::create() != nullptr; }

std::optional<Camera> CameraBle::find_camera(std::chrono::milliseconds timeout, const std::string& address,
                                             std::stop_token stop) {
    impl_->central = detail::Central::create();
    if (!impl_->central) {
        impl_->say("Bluetooth is off or there is no Bluetooth adapter");
        return std::nullopt;
    }
    const std::string wanted = detail::normalize_address(address);
    std::optional<Camera> found;
    bool found_preferred = false;
    std::chrono::steady_clock::time_point preferred_since;
    // Takes the first DJI camera seen; a camera with the preferred address replaces it.
    impl_->central->scan(timeout, [&](const detail::Advertisement& seen) {
        const auto dji = seen.manufacturer_data.find(kDjiCompanyId);
        if (dji == seen.manufacturer_data.end()) {
            return false;
        }
        const bool preferred = wanted.empty() || seen.address == wanted;
        if (!found || (preferred && !found_preferred) || found->address == seen.address) {
            if (preferred && !found_preferred) {
                preferred_since = std::chrono::steady_clock::now();
            }
            found = Camera{seen.name, seen.address, seen.rssi, dji->second.empty() ? std::uint8_t{0} : dji->second[0]};
            found_preferred = preferred;
        }
        // The name arrives in a separate scan response: give it a moment.
        return found_preferred && (!found->name.empty() || std::chrono::steady_clock::now() - preferred_since > kNameWait);
    }, stop);
    if (found) {
        impl_->scanned_address = found->address;
    }
    return found;
}

bool CameraBle::connect(const Camera& camera) {
    if (!impl_->central || impl_->scanned_address != camera.address) {
        impl_->say("camera not scanned");
        return false;
    }
    std::string error;
    impl_->link = impl_->central->connect(camera.address, &error);
    if (!impl_->link) {
        impl_->say("connect failed: " + error);
        return false;
    }
    impl_->start_writer();
    auto on_notify = [impl = impl_.get()](const Bytes& data) { impl->on_notify(data); };
    if (!impl_->link->subscribe(kService, kNotify, on_notify)) {
        impl_->say("connect failed: the camera's notify characteristic is missing");
        disconnect();
        return false;
    }
    impl_->link->subscribe(kService, kWrite, on_notify);  // may not support notifications
    // Writing 01 00 to the notify characteristic arms pairing on DJI cameras.
    impl_->link->write(kService, kNotify, {0x01, 0x00}, true);
    std::this_thread::sleep_for(200ms);
    return true;
}

bool CameraBle::connected() const { return impl_->link && impl_->link->connected(); }

void CameraBle::disconnect() {
    impl_->writer = {};  // stops and joins the writer before the link goes away
    if (impl_->link) {
        impl_->link->disconnect();
        impl_->link.reset();
    }
}

PairResult CameraBle::pair(const std::string& identifier, const std::string& token,
                           std::chrono::seconds approval_timeout, const std::function<void()>& on_approval_needed,
                           std::stop_token stop) {
    impl_->send(duml::Frame{duml::kAddrApp, duml::kAddrSession, impl_->next_seq++, duml::kFlagRequest, 0x00, 0x2B,
                            {0x04, 0x00}});  // session open (DJI Mimo sends this first)
    std::this_thread::sleep_for(120ms);
    Bytes payload = duml::pack_string(identifier);
    const Bytes token_bytes = duml::pack_string(token);
    payload.insert(payload.end(), token_bytes.begin(), token_bytes.end());
    for (int attempt = 0; attempt < 3 && !stop.stop_requested(); ++attempt) {
        const auto reply = impl_->request(duml::kAddrWifi, 0x07, 0x45, payload, 2500ms, kPairingSeq);
        if (!reply || reply->payload.size() < 2) {
            continue;
        }
        if (reply->payload[1] == 0x01) {
            return PairResult::AlreadyPaired;
        }
        if (reply->payload[1] == 0x02) {
            if (on_approval_needed) {
                on_approval_needed();
            }
            std::unique_lock lock(impl_->mutex);
            if (impl_->changed.wait_for(lock, stop, approval_timeout, [this] { return impl_->approved; })) {
                return PairResult::Approved;
            }
            return stop.stop_requested() ? PairResult::Failed : PairResult::TimedOut;
        }
    }
    return PairResult::Failed;
}

std::optional<WifiCredentials> CameraBle::wake_wifi() {
    impl_->request(duml::kAddrSystem, 0x53, 0x10, Bytes(4, 0x00), 1500ms);  // wake; reply varies
    std::this_thread::sleep_for(800ms);
    const auto ssid = parse_string_reply(impl_->request(duml::kAddrWifi, 0x07, 0x07, {}, 2000ms));
    std::this_thread::sleep_for(500ms);
    const auto password = parse_string_reply(impl_->request(duml::kAddrWifi, 0x07, 0x0E, {}, 2000ms));
    if (!ssid || ssid->empty() || !password) {
        impl_->say("camera did not return its Wi-Fi credentials (not activated?)");
        return std::nullopt;
    }
    return WifiCredentials{*ssid, *password};
}

void CameraBle::keepalive() {
    impl_->send(duml::Frame{duml::kAddrApp, duml::kAddrSession, impl_->next_seq++, duml::kFlagRequest, 0x00, 0x2B,
                            {0x01, 0x01}});
}

std::string detail::normalize_address(std::string address) {
    std::transform(address.begin(), address.end(), address.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return address;
}

std::vector<NearbyDevice> scan_nearby(std::chrono::milliseconds duration) {
    const auto central = detail::Central::create();
    if (!central) {
        return {};
    }
    std::map<std::string, NearbyDevice> devices;
    central->scan(duration, [&](const detail::Advertisement& seen) {
        NearbyDevice& device = devices[seen.address];
        device = NearbyDevice{seen.name, seen.address, seen.rssi, std::nullopt};
        if (const auto dji = seen.manufacturer_data.find(kDjiCompanyId); dji != seen.manufacturer_data.end()) {
            device.dji_model = dji->second.empty() ? std::uint8_t{0} : dji->second[0];
        }
        return false;  // keep listening for the whole duration
    });
    std::vector<NearbyDevice> out;
    for (auto& [address, device] : devices) {
        out.push_back(std::move(device));
    }
    std::sort(out.begin(), out.end(), [](const NearbyDevice& a, const NearbyDevice& b) { return a.rssi > b.rssi; });
    return out;
}

std::string make_identifier() {
    std::random_device random;
    std::string out;
    for (int i = 0; i < 32; ++i) {
        out += "0123456789abcdef"[random() % 16];
    }
    return out;
}

}  // namespace djivcam::ble
