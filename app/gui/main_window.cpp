#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QScreen>
#include <QStackedWidget>
#include <QScrollArea>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QSignalBlocker>
#include <QToolButton>

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <chrono>

#include "bridge_link.h"
#include "camera_panel.h"
#include "djivcam/camera_ble.h"
#include "pipeline.h"
#include "preview_widget.h"

#ifdef DJIVCAM_HAVE_VCAM
#include "djivcam/virtual_camera.h"
#endif

using djivcam::media::DecoderPreference;
using Stage = CameraConnector::Stage;

namespace {

const QString kIdentifierKey = QStringLiteral("camera/identifier");
const QString kAddressKey = QStringLiteral("camera/address");
const QString kBluetoothKey = QStringLiteral("connect/wakeOverBluetooth");
const QString kBridgeKey = QStringLiteral("connect/configureBridge");
const QString kStartupKey = QStringLiteral("connect/onStartup");
const QString kDecoderKey = QStringLiteral("video/decoder");
const QString kVirtualCameraKey = QStringLiteral("vcam/enabled");
const QString kHoldOnLossKey = QStringLiteral("video/holdOnLoss");
// Advanced, no UI: the camera's address (its own access point: 192.168.2.1; 127.0.0.1 for
// tools/fake_camera.py).
const QString kCameraIpKey = QStringLiteral("connect/cameraIp");
const QString kPairingToken = QStringLiteral("obsd");  // shown on the camera's approval prompt
constexpr qint64 kRewakeAfterMs = 10000;                // camera network gone this long: wake again

QAction* add_toggle(QMenu* menu, const QString& text, QSettings* settings, const QString& key, bool fallback) {
    QAction* action = menu->addAction(text);
    action->setCheckable(true);
    action->setChecked(settings->value(key, fallback).toBool());
    QObject::connect(action, &QAction::toggled, menu, [settings, key](bool on) { settings->setValue(key, on); });
    return action;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      settings_(new QSettings(this)),
      pipeline_(new Pipeline(this)),
      connector_(new CameraConnector(this)),
      preview_(new PreviewWidget(this)),
      view_(new QStackedWidget(this)),
      status_view_(new QLabel(this)),
      camera_panel_(new CameraPanel(this)),
      connect_action_(new QAction(tr("Connect"), this)),
      decoder_choice_(new QComboBox(this)),
      camera_label_(new QLabel(this)),
      state_label_(new QLabel(tr("Not connected: click Connect"), this)),
      format_label_(new QLabel(this)),
      stats_label_(new QLabel(this)),
      decoder_label_(new QLabel(this)) {
    vcam_action_ = new QAction(tr("Virtual camera"), this);
    vcam_label_ = new QLabel(this);
    setWindowTitle(tr("DJI VCam - DJI Osmo Action live view"));
    status_view_->setAlignment(Qt::AlignCenter);
    status_view_->setWordWrap(true);
    status_view_->setMargin(40);
    status_view_->setAutoFillBackground(true);
    QPalette dark = status_view_->palette();
    dark.setColor(QPalette::Window, Qt::black);
    status_view_->setPalette(dark);
    view_->addWidget(status_view_);
    view_->addWidget(preview_);
    setCentralWidget(view_);
    // Room for the preview and the camera settings, but never larger than the screen.
    const QRect available = screen()->availableGeometry();
    resize(std::min(1560, available.width() * 9 / 10), std::min(820, available.height() * 9 / 10));

    auto* camera_dock = new QDockWidget(tr("Camera settings"), this);
    camera_dock->setObjectName(QStringLiteral("cameraSettings"));
    camera_dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    auto* camera_scroll = new QScrollArea(camera_dock);
    camera_scroll->setWidgetResizable(true);
    camera_scroll->setWidget(camera_panel_);
    camera_scroll->setMinimumWidth(300);
    camera_dock->setWidget(camera_scroll);
    addDockWidget(Qt::RightDockWidgetArea, camera_dock);

    decoder_choice_->addItem(tr("Decoder: auto (GPU if available)"), int(DecoderPreference::Auto));
    decoder_choice_->addItem(tr("Decoder: GPU"), int(DecoderPreference::Hardware));
    decoder_choice_->addItem(tr("Decoder: CPU"), int(DecoderPreference::Software));
    decoder_choice_->setCurrentIndex(std::max(0, decoder_choice_->findData(settings_->value(kDecoderKey, 0).toInt())));
    connect(decoder_choice_, &QComboBox::currentIndexChanged, this,
            [this] { settings_->setValue(kDecoderKey, decoder_choice_->currentData()); });

    auto* options = new QMenu(tr("Options"), this);
    bluetooth_action_ = add_toggle(options, tr("Wake the camera over Bluetooth"), settings_, kBluetoothKey, true);
    bridge_action_ = add_toggle(options, tr("Configure the ESP32 USB bridge automatically"), settings_, kBridgeKey, true);
    startup_action_ = add_toggle(options, tr("Connect on startup"), settings_, kStartupKey, false);
    QAction* hold_action = add_toggle(options, tr("Freeze the picture after lost video (instead of showing damaged frames)"),
                                      settings_, kHoldOnLossKey, false);  // real time first
    pipeline_->setHoldOnLoss(hold_action->isChecked());
    connect(hold_action, &QAction::toggled, this, [this](bool on) { pipeline_->setHoldOnLoss(on); });
    options->addSeparator();
    connect(options->addAction(tr("Forget the paired camera")), &QAction::triggered, this, &MainWindow::forgetCamera);
    auto* options_button = new QToolButton(this);
    options_button->setText(tr("Options"));
    options_button->setMenu(options);
    options_button->setPopupMode(QToolButton::InstantPopup);

    connect_action_->setCheckable(true);
    auto* toolbar = addToolBar(tr("Camera"));
    toolbar->setMovable(false);
    toolbar->addAction(connect_action_);
    toolbar->addSeparator();
    toolbar->addWidget(decoder_choice_);
    toolbar->addWidget(options_button);
    toolbar->addSeparator();
    auto* snapshot_action = toolbar->addAction(tr("Snapshot"));
    snapshot_action->setToolTip(tr("Save the current frame as a picture in Pictures\\DJI VCam"));
    connect(snapshot_action, &QAction::triggered, this, &MainWindow::saveSnapshot);
    toolbar->addSeparator();
    toolbar->addAction(camera_dock->toggleViewAction());
    toolbar->addSeparator();
    vcam_action_->setCheckable(true);
    vcam_action_->setToolTip(tr("Offer the live view as the \"DJI VCam\" webcam to OBS, Zoom, browsers and other apps"));
    toolbar->addAction(vcam_action_);
    toolbar->addSeparator();
    toolbar->addWidget(camera_label_);

    statusBar()->addWidget(state_label_, 1);
    statusBar()->addPermanentWidget(vcam_label_);
    statusBar()->addPermanentWidget(format_label_);
    statusBar()->addPermanentWidget(stats_label_);
    statusBar()->addPermanentWidget(decoder_label_);

    connect(connect_action_, &QAction::toggled, this, &MainWindow::toggleConnection);
    // Signals from the decode thread can arrive after Disconnect: ignore them then.
    connect(pipeline_, &Pipeline::frameAvailable, this, [this] {
        if (auto frame = pipeline_->takeLatestFrame(); frame && pipeline_->running()) {
            preview_->showFrame(std::move(frame));
            view_->setCurrentWidget(preview_);
        }
    });
    connect(pipeline_, &Pipeline::stateChanged, this, &MainWindow::onSessionState);
    connect(pipeline_, &Pipeline::cameraChanged, this, [this] { camera_panel_->showState(pipeline_->takeCameraState()); });
    connect(pipeline_, &Pipeline::cameraError, this, [this](const QString& message) {
        qInfo("camera: %s", qPrintable(message));
        statusBar()->showMessage(tr("Camera: %1").arg(message), 8000);
        camera_panel_->showState(pipeline_->takeCameraState());  // snap refused values back
    });
    connect(pipeline_, &Pipeline::statsUpdated, this, &MainWindow::onStats);
    connect(pipeline_, &Pipeline::decoderChanged, this, &MainWindow::onDecoder);
    connect(pipeline_, &Pipeline::formatChanged, this, [this](int width, int height) {
        if (pipeline_->running()) {
            format_label_->setText(tr("%1×%2").arg(width).arg(height));
        }
    });
    connect(pipeline_, &Pipeline::errorOccurred, this, [this](const QString& message) {
        QMessageBox::warning(this, tr("Decoder error"), message);
        connect_action_->setChecked(false);
    });
    connect(connector_, &CameraConnector::stageChanged, this, &MainWindow::onConnectorStage);
    connect(connector_, &CameraConnector::wifiReady, this, &MainWindow::onWifiReady);
    connect(connector_, &CameraConnector::cameraFound, this, [this](const QString& name, const QString& address) {
        camera_label_->setText(tr("Camera: %1").arg(name));
        settings_->setValue(kAddressKey, address);
    });

#ifdef DJIVCAM_HAVE_VCAM
    virtual_camera_ = new djivcam::vcam::VirtualCamera();
    pipeline_->setVirtualCamera(virtual_camera_);
    connect(vcam_action_, &QAction::toggled, this, &MainWindow::enableVirtualCamera);
    auto* vcam_timer = new QTimer(this);
    connect(vcam_timer, &QTimer::timeout, this, &MainWindow::updateVirtualCameraStatus);
    vcam_timer->start(1000);
    if (settings_->value(kVirtualCameraKey, true).toBool()) {
        QTimer::singleShot(0, this, [this] { vcam_action_->setChecked(true); });
    }
#else
    vcam_action_->setEnabled(false);
    vcam_action_->setToolTip(tr("The virtual camera is not available on this platform yet"));
#endif
    updateVirtualCameraStatus();
    showStage(state_label_->text());

    if (startup_action_->isChecked()) {
        QTimer::singleShot(0, this, &MainWindow::connectCamera);
    }
}

void MainWindow::enableVirtualCamera(bool on) {
#ifdef DJIVCAM_HAVE_VCAM
    using djivcam::vcam::VirtualCamera;
    if (!on) {
        virtual_camera_->stop();  // safe while the decoder thread publishes (guarded inside)
        settings_->setValue(kVirtualCameraKey, false);
        updateVirtualCameraStatus();
        return;
    }
    std::string error;
    if (!VirtualCamera::source_registered()) {
        const auto answer = QMessageBox::question(
            this, tr("Install the virtual camera"),
            tr("The \"DJI VCam\" camera component must be installed once (administrator permission is "
               "required). Install it now?"));
        const QString dll = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("djivcam-source.dll"));
        if (answer != QMessageBox::Yes ||
            !VirtualCamera::install_source(QDir::toNativeSeparators(dll).toStdWString(), &error)) {
            if (!error.empty()) {
                QMessageBox::warning(this, tr("Virtual camera"), QString::fromStdString(error));
            }
            QSignalBlocker block(vcam_action_);
            vcam_action_->setChecked(false);
            updateVirtualCameraStatus();
            return;
        }
    }
    if (!virtual_camera_->start(&error)) {
        QMessageBox::warning(this, tr("Virtual camera"), QString::fromStdString(error));
        QSignalBlocker block(vcam_action_);
        vcam_action_->setChecked(false);
    } else {
        settings_->setValue(kVirtualCameraKey, true);
    }
    updateVirtualCameraStatus();
#else
    (void)on;
#endif
}

void MainWindow::saveSnapshot() {
    const QImage image = preview_->snapshot();
    if (image.isNull()) {
        statusBar()->showMessage(tr("No video to save"), 4000);
        return;
    }
    const QDir folder(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/DJI VCam"));
    const QString path =
        folder.filePath(QStringLiteral("dji-vcam-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    if (!folder.mkpath(QStringLiteral(".")) || !image.save(path)) {
        statusBar()->showMessage(tr("Could not save the snapshot to %1").arg(QDir::toNativeSeparators(folder.path())), 6000);
        return;
    }
    statusBar()->showMessage(tr("Snapshot saved: %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void MainWindow::updateVirtualCameraStatus() {
#ifdef DJIVCAM_HAVE_VCAM
    using djivcam::vcam::VirtualCamera;
    QString text;
    if (!virtual_camera_->running()) {
        text = VirtualCamera::source_registered() ? tr("Webcam: off") : tr("Webcam: not installed");
    } else {
        text = virtual_camera_->in_use() ? tr("Webcam: in use") : tr("Webcam: ready");
    }
    vcam_label_->setText(text);
#else
    vcam_label_->setText(tr("Webcam: not available on this platform"));
#endif
}

MainWindow::~MainWindow() {
    connector_->stop();
    camera_panel_->setController(nullptr);  // the controller goes away with the pipeline's session
    pipeline_->stop();
#ifdef DJIVCAM_HAVE_VCAM
    delete virtual_camera_;
#endif
}

void MainWindow::connectCamera() { connect_action_->setChecked(true); }

void MainWindow::replayFile(const QString& path) {
    replay_file_ = path;
    camera_label_->setText(tr("Replay: %1").arg(QFileInfo(path).fileName()));
    connectCamera();
}

void MainWindow::importPairingIdentifier(const QString& identifier) { settings_->setValue(kIdentifierKey, identifier); }

QString MainWindow::pairingIdentifier() {
    QString identifier = settings_->value(kIdentifierKey).toString();
    if (identifier.isEmpty()) {
        identifier = QString::fromStdString(djivcam::ble::make_identifier());  // approved once on the camera
        settings_->setValue(kIdentifierKey, identifier);
    }
    return identifier;
}

void MainWindow::forgetCamera() {
    settings_->remove(kIdentifierKey);
    settings_->remove(kAddressKey);
    camera_label_->clear();
    QMessageBox::information(this, tr("Camera forgotten"),
                             tr("The next connection pairs again; approve the request on the camera screen."));
}

void MainWindow::toggleConnection(bool connect) {
    qInfo(connect ? "connect" : "disconnect");
    connect_action_->setText(connect ? tr("Disconnect") : tr("Connect"));
    decoder_choice_->setEnabled(!connect);
    if (!connect) {
        connector_->stop();
        camera_panel_->setController(nullptr);
        pipeline_->stop();
        showStatusView();
        streaming_ = false;
        showStage(tr("Not connected: click Connect"));
        format_label_->clear();
        stats_label_->clear();
        decoder_label_->clear();
        return;
    }
    const auto decoder = static_cast<DecoderPreference>(decoder_choice_->currentData().toInt());
    if (!replay_file_.isEmpty()) {
        camera_panel_->setController(nullptr);
        pipeline_->startReplay(replay_file_, decoder);
        return;
    }
    // The session waits for the camera network by itself; Bluetooth (if enabled) brings it up.
    djivcam::SessionConfig config;
    config.identifier = pairingIdentifier().toStdString();
    config.token = kPairingToken.toStdString();
    // With Bluetooth the datalink waits until the Bluetooth session has woken the camera and hung up.
    config.connect_allowed = !(bluetooth_action_->isChecked() && CameraConnector::bluetoothAvailable());
    const QString camera_ip = settings_->value(kCameraIpKey, QString::fromStdString(config.camera_ip)).toString();
    config.camera_ip = camera_ip.toStdString();
    config.camera_subnet_prefix = camera_ip.left(camera_ip.lastIndexOf(QLatin1Char('.')) + 1).toStdString();
    network_missing_.invalidate();
    camera_panel_->setController(nullptr);
    pipeline_->start(config, decoder);
    camera_panel_->setController(pipeline_->camera());
    if (bluetooth_action_->isChecked()) {
        if (CameraConnector::bluetoothAvailable()) {
            connector_->start(pairingIdentifier(), kPairingToken, settings_->value(kAddressKey).toString());
        } else {
            showStage(tr("Bluetooth is off or missing: join the camera's Wi-Fi another way"), true);
        }
    }
}

void MainWindow::showStage(const QString& text, bool attention) {
    state_label_->setText(text);
    // The same text, large, where the video would be (shown whenever there is no video).
    status_view_->setText(text);
    status_view_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 16pt; %2 }")
                                    .arg(attention ? QStringLiteral("#f59e0b") : QStringLiteral("#b0b0b0"),
                                         attention ? QStringLiteral("font-weight: bold;") : QString()));
    state_label_->setStyleSheet(attention ? QStringLiteral("color: #d97706; font-weight: bold;") : QString());
    if (attention) {
        QApplication::alert(this);
    }
}

void MainWindow::showStatusView() {
    preview_->clear();
    view_->setCurrentWidget(status_view_);
}

void MainWindow::onConnectorStage(Stage stage, const QString& detail) {
    qInfo("bluetooth: %s", qPrintable(detail));
    connector_stage_ = stage;
    // No datalink connection may start while a Bluetooth session is open (it would lose its video).
    if (stage == Stage::Pairing || stage == Stage::ApprovalNeeded || stage == Stage::WakingWifi) {
        pipeline_->setConnectAllowed(false);
    } else if (stage == Stage::WifiReady) {
        pipeline_->setConnectAllowed(true);
    }
    // Bluetooth stages matter until the camera's Wi-Fi is up; after that the session speaks.
    if (!streaming_ && stage != Stage::Idle && stage != Stage::WifiReady) {
        showStage(detail, stage == Stage::ApprovalNeeded || stage == Stage::Failed);
    }
}

void MainWindow::onWifiReady(const QString& ssid, const QString& password) {
    if (streaming_) {
        return;  // a Bluetooth reconnect while video flows: the network and the bridge are fine
    }
    // From now on the camera's network should appear: if it does not within kRewakeAfterMs, the
    // camera is woken again (checked in onStats()).
    network_missing_.start();
    if (!bridge_action_->isChecked()) {
        showStage(tr("Waiting for the camera's Wi-Fi (%1)").arg(ssid));
        return;
    }
    showStage(tr("Configuring the ESP32 bridge"));
    QString error;
    if (!BridgeLink::ensureCredentials(ssid, password, &error)) {
        showStage(tr("%1: join the camera's Wi-Fi another way").arg(error), true);
        return;
    }
    showStage(tr("Waiting for the camera's Wi-Fi (the bridge is joining %1)").arg(ssid));
}

void MainWindow::onSessionState(const QString& state, const QString& detail) {
    qInfo("session: %s%s", qPrintable(state), detail.isEmpty() ? "" : qPrintable(" (" + detail + ")"));
    const bool was_streaming = streaming_;
    streaming_ = state == QLatin1String("streaming");
    if (was_streaming && !streaming_) {
        showStatusView();  // no frozen picture: show what the app is doing instead
    }
    if (state == QLatin1String("waiting for camera network")) {
        if (!network_missing_.isValid()) {
            network_missing_.start();  // checked every second in onStats()
        }
        if (connector_stage_ != Stage::Idle && connector_stage_ != Stage::WifiReady) {
            return;  // a Bluetooth stage is more informative right now
        }
        showStage(tr("Waiting for the camera network%1").arg(detail.isEmpty() ? QString() : " (" + detail + ")"));
        return;
    }
    network_missing_.invalidate();
    if (state == QLatin1String("connecting")) {
        showStage(tr("Connecting to the camera%1").arg(detail.isEmpty() ? QString() : " (" + detail + ")"));
    } else if (streaming_) {
        showStage(detail.isEmpty() ? tr("Streaming") : tr("Streaming (%1)").arg(detail));
    }
}

void MainWindow::onStats(const LiveStats& stats) {
    if (!streaming_ && network_missing_.isValid() && network_missing_.elapsed() > kRewakeAfterMs &&
        connector_stage_ == Stage::WifiReady) {
        qInfo("camera network missing for %lld ms: waking the camera again", network_missing_.elapsed());
        connector_->wakeAgain();  // the camera's access point went away
        network_missing_.restart();
    }
    if (streaming_ && ++stats_seconds_ % 10 == 0) {
        qInfo("stats: %.0f fps, %.0f kbit/s, delay %.0f ms, loss %.2f%%, %.0f held, %llu reconnects", stats.fps, stats.kbps,
              stats.delay_ms, stats.loss_percent, stats.held, static_cast<unsigned long long>(stats.reconnects));
    }
    // "delay" is the app's own share of the latency; seconds of lag with a small delay here come
    // from the camera or the radio link.
    stats_label_->setText(tr("%1 fps  |  %2 kbit/s  |  delay %3 ms  |  loss %4%  |  %5 held  |  %6 dup  |  %7 reconnects")
                              .arg(stats.fps, 0, 'f', 0)
                              .arg(stats.kbps, 0, 'f', 0)
                              .arg(stats.delay_ms, 0, 'f', 0)
                              .arg(stats.loss_percent, 0, 'f', 2)
                              .arg(stats.held, 0, 'f', 0)
                              .arg(stats.duplicates)
                              .arg(stats.reconnects));
}

void MainWindow::onDecoder(const QString& backend, bool hardware) {
    decoder_label_->setText(tr("decoder: %1 (%2)").arg(backend, hardware ? tr("GPU") : tr("CPU")));
}
