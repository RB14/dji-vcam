#include "djivcam/camera_ble.h"

#include <simpleble/SimpleBLE.h>

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "djivcam/duml.h"

namespace djivcam::ble {
namespace {

using namespace std::chrono_literals;
using duml::Bytes;

const std::string kService = "0000fff0-0000-1000-8000-00805f9b34fb";
const std::string kNotify = "0000fff4-0000-1000-8000-00805f9b34fb";
const std::string kWrite = "0000fff5-0000-1000-8000-00805f9b34fb";

constexpr std::uint16_t kPairingSeq = 0x8092;
constexpr auto kWriteSpacing = 20ms;  // back-to-back write-without-response frames get dropped

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
    std::optional<SimpleBLE::Adapter> adapter;
    std::optional<SimpleBLE::Peripheral> peripheral;

    std::mutex mutex;
    std::condition_variable changed;
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
                try {
                    if (peripheral && peripheral->is_connected()) {
                        peripheral->write_command(kService, kWrite, SimpleBLE::ByteArray(frame));
                    }
                } catch (const std::exception& error) {
                    say(std::string("write failed: ") + error.what());
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
    void on_notify(const SimpleBLE::ByteArray& data) {
        const std::vector<std::uint8_t> bytes = data;
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

bool CameraBle::bluetooth_available() {
    try {
        return SimpleBLE::Adapter::bluetooth_enabled() && !SimpleBLE::Adapter::get_adapters().empty();
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<Camera> CameraBle::find_camera(std::chrono::milliseconds timeout, const std::string& address) {
    auto adapters = SimpleBLE::Adapter::get_adapters();
    if (adapters.empty()) {
        impl_->say("no Bluetooth adapter");
        return std::nullopt;
    }
    impl_->adapter = adapters.front();
    std::mutex found_mutex;
    std::condition_variable found_changed;
    std::optional<Camera> found;
    std::optional<SimpleBLE::Peripheral> found_peripheral;
    bool found_preferred = false;
    // Takes the first DJI camera seen; a camera with the preferred address replaces it.
    auto consider = [&](SimpleBLE::Peripheral peripheral) {
        const auto data = peripheral.manufacturer_data();
        const auto dji = data.find(kDjiCompanyId);
        if (dji == data.end()) {
            return;
        }
        const bool preferred = address.empty() || peripheral.address() == address;
        std::lock_guard lock(found_mutex);
        if (found && (found_preferred || !preferred)) {
            return;
        }
        found = Camera{peripheral.identifier(), peripheral.address(), peripheral.rssi(),
                       dji->second.empty() ? std::uint8_t{0} : dji->second.data()[0]};
        found_peripheral = peripheral;
        found_preferred = preferred;
        found_changed.notify_all();
    };
    impl_->adapter->set_callback_on_scan_found(consider);
    impl_->adapter->set_callback_on_scan_updated(consider);
    impl_->adapter->scan_start();
    {
        std::unique_lock lock(found_mutex);
        found_changed.wait_for(lock, timeout, [&] { return found_preferred; });
    }
    impl_->adapter->scan_stop();
    impl_->adapter->set_callback_on_scan_found(nullptr);
    impl_->adapter->set_callback_on_scan_updated(nullptr);
    std::lock_guard lock(found_mutex);
    if (found) {
        impl_->peripheral = found_peripheral;
    }
    return found;
}

bool CameraBle::connect(const Camera& camera) {
    if (!impl_->peripheral || impl_->peripheral->address() != camera.address) {
        impl_->say("camera not scanned");
        return false;
    }
    try {
        impl_->peripheral->connect();
        impl_->start_writer();
        impl_->peripheral->notify(kService, kNotify, [this](SimpleBLE::ByteArray data) { impl_->on_notify(data); });
        try {
            impl_->peripheral->notify(kService, kWrite, [this](SimpleBLE::ByteArray data) { impl_->on_notify(data); });
        } catch (const std::exception&) {
            // the write characteristic may not support notifications
        }
        // Writing 01 00 to the notify characteristic arms pairing on DJI cameras.
        impl_->peripheral->write_request(kService, kNotify, SimpleBLE::ByteArray({0x01, 0x00}));
        std::this_thread::sleep_for(200ms);
        return true;
    } catch (const std::exception& error) {
        impl_->say(std::string("connect failed: ") + error.what());
        return false;
    }
}

bool CameraBle::connected() const {
    try {
        return impl_->peripheral && impl_->peripheral->is_connected();
    } catch (const std::exception&) {
        return false;
    }
}

void CameraBle::disconnect() {
    impl_->writer = {};
    try {
        if (impl_->peripheral && impl_->peripheral->is_connected()) {
            impl_->peripheral->disconnect();
        }
    } catch (const std::exception&) {
    }
}

PairResult CameraBle::pair(const std::string& identifier, const std::string& token,
                           std::chrono::seconds approval_timeout, const std::function<void()>& on_approval_needed) {
    impl_->send(duml::Frame{duml::kAddrApp, duml::kAddrSession, impl_->next_seq++, duml::kFlagRequest, 0x00, 0x2B,
                            {0x04, 0x00}});  // session open (DJI Mimo sends this first)
    std::this_thread::sleep_for(120ms);
    Bytes payload = duml::pack_string(identifier);
    const Bytes token_bytes = duml::pack_string(token);
    payload.insert(payload.end(), token_bytes.begin(), token_bytes.end());
    for (int attempt = 0; attempt < 3; ++attempt) {
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
            return impl_->changed.wait_for(lock, approval_timeout, [this] { return impl_->approved; })
                       ? PairResult::Approved
                       : PairResult::TimedOut;
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

std::string make_identifier() {
    std::random_device random;
    std::string out;
    for (int i = 0; i < 32; ++i) {
        out += "0123456789abcdef"[random() % 16];
    }
    return out;
}

}  // namespace djivcam::ble
