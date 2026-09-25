// Brings the camera's Wi-Fi up over Bluetooth, then hangs up, as DJI Mimo does: the camera ties its
// app session to the Bluetooth link, and a link still open when the camera is switched off comes
// back after a short power-off with a live view that sends no video (docs/protocol-notes.md 3.11).
// The live view runs over Wi-Fi alone; wakeAgain() connects and wakes the camera again when its
// network is gone.
//
// Runs on a worker thread and reports each stage, so the UI can say what is happening:
// searching for the camera, waiting for the on-camera pairing approval, waking its Wi-Fi.
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

class CameraConnector : public QObject {
    Q_OBJECT

public:
    enum class Stage { Idle, Searching, Pairing, ApprovalNeeded, WakingWifi, WifiReady, Failed };
    Q_ENUM(Stage)

    explicit CameraConnector(QObject* parent = nullptr);
    ~CameraConnector() override;

    static bool bluetoothAvailable();

    // identifier: pairing identifier (approved once on the camera); address: preferred camera
    // (empty = first DJI camera found).
    void start(const QString& identifier, const QString& token, const QString& address);
    void stop();
    // Connects and wakes the camera's Wi-Fi again (e.g. its access point went away).
    void wakeAgain() { wake_requested_ = true; }
    // Moves the camera's access point to this 2.4 GHz channel in the next Bluetooth session, before
    // the video connection starts (0: leave it). The camera keeps it afterwards.
    void setWifiChannel(int channel) { wifi_channel_ = channel; }

signals:
    void stageChanged(CameraConnector::Stage stage, const QString& detail);
    // The camera accepted a move of its access point to `channel`.
    void wifiChannelChanged(int channel);
    void cameraFound(const QString& name, const QString& address);
    // The camera's access point is up; credentials to give the network link (e.g. the ESP32 bridge).
    void wifiReady(const QString& ssid, const QString& password);

private:
    void run(std::stop_token stop, QString identifier, QString token, QString address);

    std::jthread worker_;
    std::atomic<bool> wake_requested_{false};
    std::atomic<int> wifi_channel_{0};
};
