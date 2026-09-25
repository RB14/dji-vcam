#include "camera_connector.h"

#include <chrono>
#include <optional>

#include "djivcam/camera_ble.h"

namespace {

using namespace std::chrono_literals;
using Stage = CameraConnector::Stage;

constexpr auto kScanWindow = 20s;
constexpr auto kApprovalTimeout = 90s;
constexpr auto kKeepaliveInterval = 1s;
constexpr auto kRetryDelay = 3s;

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

void CameraConnector::start(const QString& identifier, const QString& token, const QString& address) {
    stop();
    wake_requested_ = false;
    worker_ = std::jthread([this, identifier, token, address](std::stop_token stop) {
        run(stop, identifier, token, address);
    });
}

void CameraConnector::stop() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    emit stageChanged(Stage::Idle, {});
}

void CameraConnector::run(std::stop_token stop, QString identifier, QString token, QString address) {
    while (!stop.stop_requested()) {
        djivcam::ble::CameraBle camera;

        emit stageChanged(Stage::Searching, tr("Searching for the camera over Bluetooth (make sure it is on)"));
        std::optional<djivcam::ble::Camera> found;
        while (!stop.stop_requested() && !found) {
            found = camera.find_camera(kScanWindow, address.toStdString(), stop);
        }
        if (!found) {
            return;
        }
        emit cameraFound(QString::fromStdString(found->name), QString::fromStdString(found->address));

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

        // Wake the Wi-Fi now, and again whenever asked to while the link is up.
        wake_requested_ = true;
        while (!stop.stop_requested() && camera.connected()) {
            if (wake_requested_.exchange(false)) {
                emit stageChanged(Stage::WakingWifi, tr("Waking the camera's Wi-Fi"));
                if (const auto credentials = camera.wake_wifi()) {
                    emit stageChanged(Stage::WifiReady, tr("Camera Wi-Fi is up"));
                    emit wifiReady(QString::fromStdString(credentials->ssid),
                                   QString::fromStdString(credentials->password));
                } else {
                    emit stageChanged(Stage::Failed, tr("The camera did not bring up its Wi-Fi, retrying"));
                    wake_requested_ = true;
                    sleep_for(stop, kRetryDelay);
                }
            }
            camera.keepalive();
            sleep_for(stop, kKeepaliveInterval);
        }
        // Bluetooth link lost: reconnect and wake the Wi-Fi again (harmless if it is already up).
    }
}
