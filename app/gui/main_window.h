// Main window: live preview, connect/disconnect, options and what the app is doing right now.
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include "camera_connector.h"

class QAction;
class QComboBox;
class QLabel;
class QSettings;
class Pipeline;
class PreviewWidget;

namespace djivcam::vcam {
class VirtualCamera;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void connectCamera();
    // Replaces the stored pairing identifier (e.g. one the camera already approved).
    void importPairingIdentifier(const QString& identifier);

private:
    void toggleConnection(bool connect);
    void onSessionState(const QString& state, const QString& detail);
    void onConnectorStage(CameraConnector::Stage stage, const QString& detail);
    void onWifiReady(const QString& ssid, const QString& password);
    void onStats(double fps, double kbps, double loss_percent, quint64 recovered, quint64 reconnects);
    void onDecoder(const QString& backend, bool hardware);
    void forgetCamera();
    void showStage(const QString& text, bool attention = false);
    QString pairingIdentifier();
    void enableVirtualCamera(bool on);
    void updateVirtualCameraStatus();

    QSettings* settings_;
    Pipeline* pipeline_;
    CameraConnector* connector_;
    PreviewWidget* preview_;
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

    CameraConnector::Stage connector_stage_ = CameraConnector::Stage::Idle;
    bool streaming_ = false;
    QElapsedTimer network_missing_;  // how long the session has been waiting for the camera network
};
