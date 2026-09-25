// Main window: live preview, connect/disconnect, options and what the app is doing right now.
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include "camera_connector.h"

class QAction;
class CameraPanel;
class QStackedWidget;
class QThread;
class QComboBox;
class QLabel;
class QSettings;
class Pipeline;
class PreviewWidget;
struct LiveStats;

namespace djivcam::vcam {
class VirtualCamera;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void connectCamera();
    // Plays a recorded H.264 stream instead of connecting to the camera (testing without one).
    void replayFile(const QString& path);
    // Replaces the stored pairing identifier (e.g. one the camera already approved).
    void importPairingIdentifier(const QString& identifier);

private:
    void toggleConnection(bool connect);
    void onSessionState(const QString& state, const QString& detail);
    void onConnectorStage(CameraConnector::Stage stage, const QString& detail);
    void onWifiReady(const QString& ssid, const QString& password);
    // Picks the camera's Wi-Fi channel for this Connect (Options: automatic or fixed); the connector
    // moves it in its Bluetooth session.
    void chooseWifiChannel();
    void onStats(const LiveStats& stats);
    void onDecoder(const QString& backend, const QString& gpu, bool hardware);
    // The camera's last known Wi-Fi channel, in the settings and the status bar (0: unknown).
    void setKnownWifiChannel(int channel);
    void showWifiChannel();
    void forgetCamera();
    void showStage(const QString& text, bool attention = false);
    // Replaces the video by the status label (not connected, connecting, video lost).
    void showStatusView();
    QString pairingIdentifier();
    void enableVirtualCamera(bool on);
    // Saves the current live-view frame as a PNG in Pictures\DJI VCam.
    void saveSnapshot();
    void updateVirtualCameraStatus();

    QSettings* settings_;
    Pipeline* pipeline_;
    CameraConnector* connector_;
    PreviewWidget* preview_;
    // The video, or instead of it (no video yet, or lost) a label saying what the app is doing.
    QStackedWidget* view_;
    QLabel* status_view_;
    CameraPanel* camera_panel_;
    QAction* connect_action_;
    QAction* bluetooth_action_;
    QAction* bridge_action_;
    QAction* startup_action_;
    QAction* vcam_action_;
    QLabel* vcam_label_;
    djivcam::vcam::VirtualCamera* virtual_camera_ = nullptr;
    QComboBox* decoder_choice_;
    QLabel* camera_label_;
    QLabel* state_label_;
    QLabel* format_label_;
    QLabel* stats_label_;
    QLabel* decoder_label_;
    QLabel* wifi_label_;

    CameraConnector::Stage connector_stage_ = CameraConnector::Stage::Idle;
    bool streaming_ = false;
    QString replay_file_;  // set: "Connect" replays this file instead
    QElapsedTimer network_missing_;  // how long the session has been waiting for the camera network
    bool channel_scanned_ = false;       // the automatic channel choice scans once per app run
    QThread* channel_scan_ = nullptr;    // the bridge scan behind it (it uses the bridge's console)
    unsigned stats_seconds_ = 0;     // for a stats line in the log every 10 s
};
