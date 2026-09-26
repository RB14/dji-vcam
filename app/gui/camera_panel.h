// The camera's own controls, like the DJI Mimo app: record and photo, shooting mode, recording
// format, stabilization, FOV, exposure, white balance, color, and its battery / storage status.
//
// Every list is filled from the camera's capability lists when it has sent them (they change with
// the mode and format), else from the known values. Controls follow what the camera reports: a
// change the camera refuses snaps back to the camera's actual value.
#pragma once

#include <QWidget>

#include <vector>

#include "djivcam/camera_protocol.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QTimer;

namespace djivcam::camera {
class CameraController;
}

class CameraPanel : public QWidget {
    Q_OBJECT

public:
    explicit CameraPanel(QWidget* parent = nullptr);

    // The camera to control, or null when not connected to one.
    void setController(djivcam::camera::CameraController* controller);
    // In Live Streaming mode (the camera on a Wi-Fi network) the shooting mode, format and codec
    // are not the camera's usual ones (a change of mode would end the session, format changes snap
    // back) and recording is unverified: those controls are hidden.
    void setLiveStreaming(bool on);
    // Why there is no camera to control (e.g. on the RTMP feed); empty for the default text.
    void setUnavailable(const QString& reason);
    void showState(const djivcam::camera::CameraState& state);

private:
    struct SettingBox {
        djivcam::camera::Setting setting;
        QComboBox* box;
    };

    QComboBox* addSetting(QFormLayout* form, djivcam::camera::Setting setting, const QString& label);
    void updateSetting(const SettingBox& row);
    void updateFormat();
    // Allowed (resolution, frame rate) pairs: the camera's list, else the usual ones.
    std::vector<std::pair<int, int>> formats() const;
    void setResolution(int resolution);
    void updateWhiteBalance();
    void updateShutter();
    void updateStatus();
    void updateEnabled();

    djivcam::camera::CameraController* controller_ = nullptr;
    djivcam::camera::CameraState state_;
    bool have_state_ = false;

    QString unavailable_;
    QWidget* shooting_ = nullptr;  // mode, format and codec
    QWidget* actions_ = nullptr;   // record and photo
    QLabel* hint_;
    QLabel* status_;
    QPushButton* record_;
    QPushButton* photo_;
    QComboBox* resolution_;
    QComboBox* frame_rate_;
    QComboBox* white_balance_;
    QComboBox* shutter_;
    std::vector<SettingBox> settings_;
    QTimer* refresh_;
};
