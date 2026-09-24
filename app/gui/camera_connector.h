// Brings the camera's Wi-Fi up over Bluetooth and keeps the Bluetooth link alive while streaming.
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
    // Asks for another Wi-Fi wake-up (e.g. the camera's access point went away).
    void wakeAgain() { wake_requested_ = true; }

signals:
    void stageChanged(CameraConnector::Stage stage, const QString& detail);
    void cameraFound(const QString& name, const QString& address);
    // The camera's access point is up; credentials to give the network link (e.g. the ESP32 bridge).
    void wifiReady(const QString& ssid, const QString& password);

private:
    void run(std::stop_token stop, QString identifier, QString token, QString address);

    std::jthread worker_;
    std::atomic<bool> wake_requested_{false};
};
