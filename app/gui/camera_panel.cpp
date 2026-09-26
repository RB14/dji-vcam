#include "camera_panel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <span>
#include <string>

#include "djivcam/camera_controller.h"

using djivcam::camera::CameraState;
using djivcam::camera::Setting;

namespace {

constexpr int kModePhoto = 0x05;
constexpr int kExposureAuto = 1;
constexpr int kExposureManual = 4;
constexpr int kRefreshMs = 1000;  // re-read the settings the camera does not push (stabilization, FOV)

// Shutter speeds offered in manual exposure (1/x s).
constexpr int kShutterSpeeds[] = {8000, 6400, 5000, 4000, 3200, 2500, 2000, 1600, 1250, 1000, 800, 640, 500,
                                  400,  320,  240,  200,  160,  120,  100,  80,   60,   50,   40,   30,  25,  24};

QString duration_text(std::uint32_t seconds) {
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600)
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

// Replaces the items of `box` with (label, code) pairs unless they are the same already (keeps an
// open list stable), then selects `current` (added if the camera reports a value not in the list).
void set_items(QComboBox* box, std::vector<std::pair<QString, int>> items, std::optional<int> current,
               const std::function<QString(int)>& label_of) {
    if (current && std::none_of(items.begin(), items.end(), [&](const auto& item) { return item.second == *current; })) {
        items.emplace_back(label_of(*current), *current);
    }
    const QSignalBlocker block(box);
    bool same = box->count() == static_cast<int>(items.size());
    for (int i = 0; same && i < box->count(); ++i) {
        same = box->itemData(i).toInt() == items[static_cast<std::size_t>(i)].second;
    }
    if (!same) {
        box->clear();
        for (const auto& [label, code] : items) {
            box->addItem(label, code);
        }
    }
    box->setCurrentIndex(current ? box->findData(*current) : -1);
}

}  // namespace

CameraPanel::CameraPanel(QWidget* parent)
    : QWidget(parent),
      hint_(new QLabel(this)),
      status_(new QLabel(this)),
      record_(new QPushButton(tr("Start recording"), this)),
      photo_(new QPushButton(tr("Take photo"), this)),
      resolution_(new QComboBox(this)),
      frame_rate_(new QComboBox(this)),
      white_balance_(new QComboBox(this)),
      shutter_(new QComboBox(this)),
      refresh_(new QTimer(this)) {
    auto* layout = new QVBoxLayout(this);
    hint_->setWordWrap(true);
    layout->addWidget(hint_);

    actions_ = new QWidget(this);
    auto* actions = new QHBoxLayout(actions_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addWidget(record_);
    actions->addWidget(photo_);
    layout->addWidget(actions_);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::RichText);  // separators are entities even when no tag is shown
    layout->addWidget(status_);

    auto group = [&](const QString& title) {
        auto* box = new QGroupBox(title, this);
        auto* form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addWidget(box);
        return form;
    };

    QFormLayout* shooting = group(tr("Shooting"));
    shooting_ = shooting->parentWidget();
    addSetting(shooting, Setting::Mode, tr("Mode"));
    shooting->addRow(tr("Resolution"), resolution_);
    shooting->addRow(tr("Frame rate"), frame_rate_);
    addSetting(shooting, Setting::Codec, tr("Codec"));

    QFormLayout* image = group(tr("Image"));
    addSetting(image, Setting::Stabilization, tr("Stabilization"));
    addSetting(image, Setting::SteadyScene, tr("Scene"));
    addSetting(image, Setting::Fov, tr("FOV"));

    QFormLayout* exposure = group(tr("Exposure"));
    addSetting(exposure, Setting::ExposureMode, tr("Mode"));
    addSetting(exposure, Setting::Ev, tr("EV"));
    addSetting(exposure, Setting::Iso, tr("ISO"));
    exposure->addRow(tr("Shutter"), shutter_);
    addSetting(exposure, Setting::IsoAutoMax, tr("Auto ISO limit"));
    addSetting(exposure, Setting::AntiFlicker, tr("Anti-flicker"));

    QFormLayout* color = group(tr("Color"));
    color->addRow(tr("White balance"), white_balance_);
    addSetting(color, Setting::Color, tr("Profile"));
    addSetting(color, Setting::Texture, tr("Texture"));
    addSetting(color, Setting::NoiseReduction, tr("Noise reduction"));
    layout->addStretch(1);

    connect(record_, &QPushButton::clicked, this, [this] {
        if (controller_) {
            controller_->record(!state_.recording);
        }
    });
    connect(photo_, &QPushButton::clicked, this, [this] {
        if (controller_) {
            controller_->take_photo();
        }
    });
    connect(resolution_, &QComboBox::activated, this, [this](int index) { setResolution(resolution_->itemData(index).toInt()); });
    connect(frame_rate_, &QComboBox::activated, this, [this](int index) {
        if (controller_ && state_.resolution) {
            controller_->set_format(*state_.resolution, frame_rate_->itemData(index).toInt());
        }
    });
    connect(white_balance_, &QComboBox::activated, this, [this](int index) {
        if (controller_) {
            controller_->set_white_balance(white_balance_->itemData(index).toInt());
        }
    });
    connect(shutter_, &QComboBox::activated, this, [this](int index) {
        if (controller_) {
            controller_->set_shutter(shutter_->itemData(index).toInt());
        }
    });
    refresh_->setInterval(kRefreshMs);
    connect(refresh_, &QTimer::timeout, this, [this] {
        if (controller_) {
            controller_->refresh();
        }
    });
    setController(nullptr);
}

QComboBox* CameraPanel::addSetting(QFormLayout* form, Setting setting, const QString& label) {
    auto* box = new QComboBox(this);
    form->addRow(label, box);
    settings_.push_back({setting, box});
    connect(box, &QComboBox::activated, this, [this, setting, box](int index) {
        if (controller_) {
            controller_->set(setting, box->itemData(index).toInt());
        }
    });
    return box;
}

void CameraPanel::setController(djivcam::camera::CameraController* controller) {
    controller_ = controller;
    have_state_ = false;
    state_ = {};
    if (controller_) {
        refresh_->start();
    } else {
        refresh_->stop();
    }
    showState(state_);
}

void CameraPanel::showState(const CameraState& state) {
    state_ = state;
    have_state_ = have_state_ || !state.values.empty() || state.resolution.has_value();
    for (const SettingBox& row : settings_) {
        updateSetting(row);
    }
    updateFormat();
    updateWhiteBalance();
    updateShutter();
    updateStatus();
    updateEnabled();
}

void CameraPanel::updateSetting(const SettingBox& row) {
    std::vector<std::pair<QString, int>> items;
    const auto allowed = state_.allowed.find(row.setting);
    const bool camera_list = allowed != state_.allowed.end() && !allowed->second.empty();
    for (const auto& choice : djivcam::camera::choices(row.setting)) {
        const bool offered = camera_list ? std::ranges::find(allowed->second, choice.code) != allowed->second.end()
                                         : choice.offered_by_default;
        if (offered) {
            items.emplace_back(QString::fromUtf8(choice.label.data(), static_cast<int>(choice.label.size())), choice.code);
        }
    }
    if (camera_list) {  // codes the camera offers that the app has no name for
        for (int code : allowed->second) {
            if (std::none_of(items.begin(), items.end(), [&](const auto& item) { return item.second == code; })) {
                items.emplace_back(QString::fromStdString(djivcam::camera::describe(row.setting, code)), code);
            }
        }
    }
    const auto current = state_.values.find(row.setting);
    set_items(row.box, std::move(items), current != state_.values.end() ? std::optional(current->second) : std::nullopt,
              [&](int code) { return QString::fromStdString(djivcam::camera::describe(row.setting, code)); });
}

std::vector<std::pair<int, int>> CameraPanel::formats() const {
    if (!state_.allowed_formats.empty()) {
        return state_.allowed_formats;
    }
    std::vector<std::pair<int, int>> usual;  // the camera has not listed them yet
    for (const auto& resolution : djivcam::camera::resolutions()) {
        for (const auto& rate : djivcam::camera::frame_rates()) {
            const bool high_rate = rate.code == 0x0A || rate.code == 0x07 || rate.code == 0x13 || rate.code == 0x08;
            const bool four_three = resolution.code == 0x67 || resolution.code == 0x5F;
            const bool very_high = rate.code == 0x13 || rate.code == 0x08;  // 200 / 240 fps: 1080p only
            if (resolution.offered_by_default && !(four_three && high_rate) && !(very_high && resolution.code != 0x0A)) {
                usual.emplace_back(resolution.code, rate.code);
            }
        }
    }
    return usual;
}

void CameraPanel::updateFormat() {
    const auto allowed = formats();
    const auto label = [](std::span<const djivcam::camera::Choice> list, int code) {
        for (const auto& choice : list) {
            if (choice.code == code) {
                return QString::fromUtf8(choice.label.data(), static_cast<int>(choice.label.size()));
            }
        }
        return QStringLiteral("code 0x%1").arg(code, 2, 16, QLatin1Char('0'));
    };
    // Resolutions in the table's order (largest first), frame rates ascending.
    std::vector<std::pair<QString, int>> resolutions;
    for (const auto& resolution : djivcam::camera::resolutions()) {
        if (std::ranges::any_of(allowed, [&](const auto& format) { return format.first == resolution.code; })) {
            resolutions.emplace_back(label(djivcam::camera::resolutions(), resolution.code), resolution.code);
        }
    }
    set_items(resolution_, std::move(resolutions), state_.resolution,
              [&](int code) { return label(djivcam::camera::resolutions(), code); });

    std::vector<std::pair<QString, int>> rates;
    for (const auto& rate : djivcam::camera::frame_rates()) {
        if (std::ranges::any_of(allowed, [&](const auto& format) {
                return format.second == rate.code && (!state_.resolution || format.first == *state_.resolution);
            })) {
            rates.emplace_back(tr("%1 fps").arg(label(djivcam::camera::frame_rates(), rate.code)), rate.code);
        }
    }
    set_items(frame_rate_, std::move(rates), state_.frame_rate,
              [&](int code) { return tr("%1 fps").arg(label(djivcam::camera::frame_rates(), code)); });
}

void CameraPanel::setResolution(int resolution) {
    if (!controller_) {
        return;
    }
    // One command sets both: keep the frame rate if the new resolution allows it, else take the
    // allowed rate closest to it.
    const auto fps_of = [](int code) {
        for (const auto& rate : djivcam::camera::frame_rates()) {
            if (rate.code == code) {
                return std::stoi(std::string(rate.label));
            }
        }
        return 0;
    };
    const int current = state_.frame_rate.value_or(0x03);  // 30 fps if unknown
    std::optional<int> best;
    for (const auto& [res, rate] : formats()) {
        if (res == resolution && (!best || std::abs(fps_of(rate) - fps_of(current)) < std::abs(fps_of(*best) - fps_of(current)))) {
            best = rate;
        }
    }
    controller_->set_format(resolution, best.value_or(current));
}

void CameraPanel::updateWhiteBalance() {
    std::vector<std::pair<QString, int>> items = {{tr("Auto"), 0}};
    for (int kelvin = 2000; kelvin <= 10000; kelvin += 100) {
        items.emplace_back(tr("%1 K").arg(kelvin), kelvin);
    }
    set_items(white_balance_, std::move(items), state_.white_balance_kelvin, [this](int kelvin) { return tr("%1 K").arg(kelvin); });
}

void CameraPanel::updateShutter() {
    // A video frame cannot be exposed longer than it lasts: at 30 fps, 1/30 s is the slowest (the
    // camera turns 1/24 into 1/30).
    const int fps = state_.frame_rate ? djivcam::camera::frames_per_second(*state_.frame_rate) : 0;
    std::vector<std::pair<QString, int>> items;
    for (int speed : kShutterSpeeds) {
        if (speed >= fps) {
            items.emplace_back(QStringLiteral("1/%1").arg(speed), speed);
        }
    }
    // In auto exposure this is the metered shutter speed; seconds-long ones cannot be set here.
    std::optional<int> current;
    if (state_.shutter_actual && *state_.shutter_actual > 0) {
        current = *state_.shutter_actual;
    }
    set_items(shutter_, std::move(items), current, [](int speed) { return QStringLiteral("1/%1").arg(speed); });
}

void CameraPanel::updateStatus() {
    QStringList parts;
    if (state_.recording) {
        parts << tr("<b style='color:#dc2626'>● REC %1</b>").arg(duration_text(state_.record_seconds.value_or(0)));
    }
    if (state_.battery_percent) {
        parts << (state_.charging ? tr("Battery %1% (charging)") : tr("Battery %1%")).arg(*state_.battery_percent);
    }
    if (state_.storage) {
        const auto& storage = *state_.storage;
        if (!storage.present) {
            parts << tr("No memory card");
        } else {
            parts << tr("%1 GB free, %2 of video left")
                         .arg(storage.free_mb / 1024.0, 0, 'f', 1)
                         .arg(duration_text(storage.video_seconds_left));
        }
    }
    if (state_.iso_actual && state_.shutter_actual && *state_.shutter_actual > 0) {
        parts << tr("Metered: ISO %1, 1/%2").arg(*state_.iso_actual).arg(*state_.shutter_actual);
    }
    status_->setText(parts.join(QStringLiteral(" &nbsp;|&nbsp; ")));
    status_->setVisible(!parts.isEmpty());

    record_->setText(state_.recording ? tr("Stop recording") : tr("Start recording"));
    record_->setStyleSheet(state_.recording ? QStringLiteral("color: #dc2626; font-weight: bold;") : QString());
    if (!controller_) {
        hint_->setText(unavailable_.isEmpty() ? tr("Connect to the camera to change its settings.") : unavailable_);
    } else if (!have_state_) {
        hint_->setText(tr("Waiting for the camera's settings..."));
    } else if (parameters_only_) {
        hint_->setText(tr("On the RTMP feed only stabilization, scene, FOV and the auto ISO limit can be changed; "
                          "exposure and color are changed on the low-latency feed (the camera keeps them)."));
    } else {
        hint_->setText(tr("Changes are made on the camera; a value it does not accept in the current mode snaps back."));
    }
}

void CameraPanel::setLiveStreaming(bool on) {
    shooting_->setVisible(!on);
    actions_->setVisible(!on);
}

void CameraPanel::setUnavailable(const QString& reason) {
    unavailable_ = reason;
    updateStatus();
}

void CameraPanel::setParametersOnly(bool on) {
    parameters_only_ = on;
    updateEnabled();
    updateStatus();
}

void CameraPanel::updateEnabled() {
    const bool live = controller_ && have_state_;
    const auto value = [this](Setting setting) -> std::optional<int> {
        const auto it = state_.values.find(setting);
        return it == state_.values.end() ? std::nullopt : std::optional(it->second);
    };
    const bool manual = value(Setting::ExposureMode) == kExposureManual;
    const bool automatic = value(Setting::ExposureMode).value_or(kExposureAuto) != kExposureManual;
    for (const SettingBox& row : settings_) {
        bool enabled = live;
        switch (row.setting) {
        case Setting::Mode:
        case Setting::Stabilization:
        case Setting::Fov:
        case Setting::Color:
        case Setting::Codec: enabled = enabled && !state_.recording; break;  // the camera refuses while recording
        case Setting::Iso: enabled = enabled && manual; break;
        case Setting::Ev:
        case Setting::IsoAutoMax:
        case Setting::AntiFlicker: enabled = enabled && automatic; break;
        default: break;
        }
        if (parameters_only_ && !djivcam::camera::parameter_of(row.setting)) {
            enabled = false;  // its command is ignored over Bluetooth
        }
        row.box->setEnabled(enabled);
    }
    resolution_->setEnabled(live && !state_.recording);
    frame_rate_->setEnabled(live && !state_.recording && state_.resolution.has_value());
    shutter_->setEnabled(live && manual && !parameters_only_);
    white_balance_->setEnabled(live && !parameters_only_);
    record_->setEnabled(live);
    photo_->setEnabled(live && value(Setting::Mode) == kModePhoto && !state_.recording);
}
