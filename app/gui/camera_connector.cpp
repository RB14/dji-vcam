#include "camera_connector.h"

#include <chrono>

#include "djivcam/camera_ble.h"
#include "djivcam/live_session.h"

namespace {

using namespace std::chrono_literals;
using Stage = CameraConnector::Stage;

constexpr auto kScanWindow = 20s;
constexpr auto kApprovalTimeout = 90s;
constexpr auto kRetryDelay = 3s;
constexpr auto kNetworkScan = 20s;  // the camera answers a scan within seconds
constexpr auto kTick = 100ms;       // requests and the keep-alive are checked this often

// Sleeps in small steps so stop requests are honoured promptly.
void sleep_for(std::stop_token stop, std::chrono::milliseconds duration) {
    const auto until = std::chrono::steady_clock::now() + duration;
    while (!stop.stop_requested() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(50ms);
    }
}

}  // namespace

CameraConnector::CameraConnector(QObject* parent) : QObject(parent) {}

CameraConnector::~CameraConnector() { stop(); }

bool CameraConnector::bluetoothAvailable() { return djivcam::ble::CameraBle::bluetooth_available(); }

void CameraConnector::start(const QString& identifier, const QString& token, const QString& address, const QString& ssid,
                            const QString& password) {
    stop();
    {
        std::lock_guard lock(mutex_);
        pending_ = {};
        if (!ssid.isEmpty()) {
            pending_.network = std::pair{ssid, password};
        }
    }
    worker_ = std::jthread([this, identifier, token, address](std::stop_token stop) { run(stop, identifier, token, address); });
}

void CameraConnector::setNetwork(const QString& ssid, const QString& password) {
    {
        std::lock_guard lock(mutex_);
        pending_.network = std::pair{ssid, password};
    }
    requested_.notify_all();
}

void CameraConnector::rescan() {
    {
        std::lock_guard lock(mutex_);
        pending_.rescan = true;
    }
    requested_.notify_all();
}

void CameraConnector::startStream(const djivcam::live::StreamSettings& settings) {
    {
        std::lock_guard lock(mutex_);
        pending_.stream = settings;
    }
    requested_.notify_all();
}

void CameraConnector::stop() {
    if (worker_.joinable()) {
        worker_.request_stop();
        requested_.notify_all();
        worker_.join();
    }
    emit stageChanged(Stage::Idle, {});
}

CameraConnector::Request CameraConnector::take_request(std::stop_token stop, std::chrono::milliseconds wait) {
    std::unique_lock lock(mutex_);
    requested_.wait_for(lock, stop, wait, [this] { return pending_.network || pending_.rescan || pending_.stream; });
    return std::exchange(pending_, {});
}

void CameraConnector::run(std::stop_token stop, QString identifier, QString token, QString address) {
    auto log = [](const std::string& message) { qInfo("bluetooth: %s", message.c_str()); };
    std::optional<std::pair<QString, QString>> network = take_request(stop, 0ms).network;
    while (!stop.stop_requested()) {
        djivcam::ble::CameraBle camera(log);
        djivcam::live::Session session(camera, log);

        emit stageChanged(Stage::Searching, tr("Searching for the camera over Bluetooth (make sure it is on)"));
        std::optional<djivcam::ble::Camera> found;
        while (!stop.stop_requested() && !found) {
            found = camera.find_camera(kScanWindow, address.toStdString(), stop);
        }
        if (!found) {
            return;
        }
        emit cameraFound(QString::fromStdString(found->name), QString::fromStdString(found->address), found->model);

        emit stageChanged(Stage::Pairing, tr("Connecting to %1").arg(QString::fromStdString(found->name)));
        if (!camera.connect(*found)) {
            emit stageChanged(Stage::Failed, tr("Bluetooth connection failed, retrying"));
            sleep_for(stop, kRetryDelay);
            continue;
        }
        const auto paired = camera.pair(identifier.toStdString(), token.toStdString(), kApprovalTimeout, [&] {
            emit stageChanged(Stage::ApprovalNeeded,
                              tr("Approve the pairing request on the camera screen (%1)").arg(token.toUpper()));
        }, stop);
        if (paired == djivcam::ble::PairResult::TimedOut || paired == djivcam::ble::PairResult::Failed) {
            emit stageChanged(Stage::Failed, paired == djivcam::ble::PairResult::TimedOut
                                                 ? tr("Pairing was not approved on the camera, retrying")
                                                 : tr("Pairing failed, retrying"));
            sleep_for(stop, kRetryDelay);
            continue;
        }

        emit stageChanged(Stage::LiveMode, tr("Switching the camera to Live Streaming mode"));
        if (!session.enter_live_mode()) {
            emit stageChanged(Stage::Failed, tr("The camera did not switch to Live Streaming mode, retrying"));
            sleep_for(stop, kRetryDelay);
            continue;
        }
        const QString mac = QString::fromStdString(session.wifi_mac().value_or(std::string()));
        bool on_network = false;
        bool rescan = !network;
        bool asked = false;  // "choose the network" announced
        while (!stop.stop_requested() && camera.connected()) {
            if (rescan) {
                emit stageChanged(Stage::Scanning, tr("Asking the camera which Wi-Fi networks it hears"));
                QList<CameraNetwork> heard;
                for (const auto& item : session.scan(kNetworkScan)) {
                    heard.append({QString::fromStdString(item.ssid), item.five_ghz});
                }
                emit networksFound(heard);
                rescan = false;
            }
            if (network && !on_network) {
                emit stageChanged(Stage::Joining, tr("The camera is joining %1").arg(network->first));
                if (!session.join(network->first.toStdString(), network->second.toStdString())) {
                    emit joinFailed(network->first);
                    network.reset();
                    continue;
                }
                on_network = true;
                emit stageChanged(Stage::Joined, tr("The camera is on %1").arg(network->first));
                emit joined(network->first, mac);
            } else if (!network && !asked) {
                emit stageChanged(Stage::NeedNetwork, tr("Choose the Wi-Fi network for the camera"));
                asked = true;
            }
            // Holds the link: the camera stays on the network only while it lives.
            Request request = take_request(stop, kTick);
            session.keep_alive();
            rescan = rescan || request.rescan;
            if (request.network) {
                if (on_network) {  // another network (or a rejoin without the push): leave, then join again
                    session.leave();
                    on_network = false;
                    if (!session.enter_live_mode()) {
                        break;
                    }
                }
                network = request.network;
                asked = false;
            }
            if (request.stream && on_network) {
                emit streamStarted(session.start_stream(*request.stream));
            }
        }
        if (stop.stop_requested()) {
            session.leave();  // Video mode: the camera goes back to its own access point
            camera.disconnect();
            return;
        }
        emit linkLost();
        emit stageChanged(Stage::Failed, tr("The Bluetooth link to the camera was lost, reconnecting"));
        sleep_for(stop, kRetryDelay);
    }
}
