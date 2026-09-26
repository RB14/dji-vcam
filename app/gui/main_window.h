// Main window: live preview, connect/disconnect, options and what the app is doing right now.
//
// Connecting puts the camera on a Wi-Fi network over Bluetooth (CameraConnector), finds its address
// there, and plays one of two feeds: the low-latency live view (the camera's own protocol) or its
// RTMP push through the local RTMP server (RtmpServer). Either one also feeds the DJI VCam webcam.
#pragma once

#include <QMainWindow>
#include <QPointer>

#include <thread>

#include "camera_connector.h"

class QAction;
class CameraPanel;
class NetworkDialog;
class QStackedWidget;
class QComboBox;
class QLabel;
class QSettings;
class Pipeline;
class PreviewWidget;
class RtmpServer;
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
    // Plays a network stream the way the RTMP feed does, without the camera (testing).
    void playStream(const QString& url);
    // Replaces the stored pairing identifier (e.g. one the camera already approved).
    void importPairingIdentifier(const QString& identifier);

private:
    enum class Feed { LowLatency, Rtmp };

    void toggleConnection(bool connect);
    void onConnectorStage(CameraConnector::Stage stage, const QString& detail);
    void onNetworksFound(const QList<CameraNetwork>& networks);
    void onJoinFailed(const QString& ssid);
    void onJoined(const QString& ssid, const QString& mac);
    // The camera's address on the network (empty: this computer does not see it).
    void onCameraAddress(const QString& ip);
    void onLinkLost();
    // Plays the chosen feed from the camera found at camera_ip_.
    void startFeed();
    void startLowLatency();
    void startRtmp();
    // Asks the camera to push to the RTMP server (once per join); false with `error` if it cannot.
    bool requestRtmp(QString* error);
    // The camera leaves and joins its network again, then the feeds start again (feed switches in
    // single-feed mode, option changes).
    void rejoin();
    // The RTMP stream runs alongside the live view (option, and the RTMP server is there).
    bool rtmpAlongside() const;
    // The camera settings panel over the Bluetooth link (the RTMP feed).
    void enableBluetoothSettings();
    void stopFeed();
    void onFeedChosen();
    void onSessionState(const QString& state, const QString& detail);
    void onStats(const LiveStats& stats);
    void onDecoder(const QString& backend, const QString& gpu, bool hardware);
    // The network dialog (the camera's scan list, a hidden network, the password).
    void askNetwork(const QString& error);
    void showStreamAddresses();
    void forgetCamera();
    void showAbout();
    void showStage(const QString& text, bool attention = false);
    // Replaces the video by the status label (not connected, connecting, video lost).
    void showStatusView();
    void updateNetworkLabel();
    QString pairingIdentifier();
    QString networkPassword() const;
    Feed feed() const;
    void enableVirtualCamera(bool on);
    // Saves the current live-view frame as a PNG in Pictures\DJI VCam.
    void saveSnapshot();
    void updateVirtualCameraStatus();

    QSettings* settings_;
    Pipeline* pipeline_;
    CameraConnector* connector_;
    RtmpServer* rtmp_ = nullptr;  // null if the app ships without one
    PreviewWidget* preview_;
    // The video, or instead of it (no video yet, or lost) a label saying what the app is doing.
    QStackedWidget* view_;
    QLabel* status_view_;
    CameraPanel* camera_panel_;
    QAction* connect_action_;
    QAction* startup_action_;
    QAction* rtmp_alongside_action_;
    QAction* vcam_action_;
    QLabel* vcam_label_;
    djivcam::vcam::VirtualCamera* virtual_camera_ = nullptr;
    QComboBox* feed_choice_;
    QComboBox* decoder_choice_;
    QLabel* camera_label_;
    QLabel* state_label_;
    QLabel* format_label_;
    QLabel* network_label_;
    QLabel* stats_label_;
    QLabel* decoder_label_;
    QPointer<NetworkDialog> network_dialog_;

    bool streaming_ = false;
    QString replay_file_;  // set: "Connect" replays this file instead
    QString stream_url_;   // set: "Connect" plays this stream instead
    QString joined_ssid_;  // the network the camera is on (empty: not joined)
    QString camera_ip_;    // its address there (empty: not known yet)
    QString camera_mac_;
    std::jthread finder_;  // looks up camera_ip_ from its MAC
    bool rtmp_requested_ = false;  // the camera was asked to push RTMP in this join
    bool rtmp_pushing_ = false;    // and it confirmed the push
    bool settings_over_bluetooth_ = false;  // the panel controls the camera through connector_
    unsigned stats_seconds_ = 0;   // for a stats line in the log every 10 s
};
