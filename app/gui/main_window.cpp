#include "main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>

#include "camera_panel.h"
#include "djivcam/camera_ble.h"
#include "djivcam/camera_model.h"
#include "djivcam/net.h"
#include "network_dialog.h"
#include "pipeline.h"
#include "preview_widget.h"
#include "rtmp_server.h"
#include "secret_store.h"

#ifdef DJIVCAM_HAVE_VCAM
#include "djivcam/virtual_camera.h"
#endif

using djivcam::media::DecoderPreference;
using Stage = CameraConnector::Stage;

namespace {

const QString kIdentifierKey = QStringLiteral("camera/identifier");
const QString kAddressKey = QStringLiteral("camera/address");
const QString kStartupKey = QStringLiteral("connect/onStartup");
const QString kNetworkKey = QStringLiteral("network/ssid");               // the network the camera joins
const QString kNetworkPasswordKey = QStringLiteral("network/password");   // secret::protect()ed
const QString kFeedKey = QStringLiteral("video/feed");                    // "lowLatency" or "rtmp"
const QString kRtmpResolutionKey = QStringLiteral("rtmp/resolution");     // 1080 or 720
const QString kDecoderKey = QStringLiteral("video/decoder");
const QString kVirtualCameraKey = QStringLiteral("vcam/enabled");
const QString kHoldOnLossKey = QStringLiteral("video/holdOnLoss");
// Advanced, no UI: play the live view straight from this address, without Bluetooth (a camera
// already on the network, or tools/fake_camera.py at 127.0.0.1).
const QString kCameraIpKey = QStringLiteral("connect/cameraIp");
const QString kPairingToken = QStringLiteral("obsd");  // shown on the camera's approval prompt
constexpr auto kFindCameraTimeout = std::chrono::seconds(20);

// The RTMP feed's choices (DJI Mimo offers these bitrates).
struct RtmpQuality {
    int resolution;
    const char* label;
    djivcam::live::Resolution code;
    std::uint16_t kbps;
};
constexpr RtmpQuality kRtmpQualities[] = {
    {1080, QT_TRANSLATE_NOOP("MainWindow", "1080p, 6 Mbit/s"), djivcam::live::Resolution::P1080, 6000},
    {720, QT_TRANSLATE_NOOP("MainWindow", "720p, 4 Mbit/s"), djivcam::live::Resolution::P720, 4000},
};

const RtmpQuality& rtmp_quality(int resolution) {
    for (const RtmpQuality& quality : kRtmpQualities) {
        if (quality.resolution == resolution) {
            return quality;
        }
    }
    return kRtmpQualities[0];
}

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
      rtmp_(RtmpServer::create(this)),
      preview_(new PreviewWidget(this)),
      view_(new QStackedWidget(this)),
      status_view_(new QLabel(this)),
      camera_panel_(new CameraPanel(this)),
      connect_action_(new QAction(tr("Connect"), this)),
      feed_choice_(new QComboBox(this)),
      decoder_choice_(new QComboBox(this)),
      camera_label_(new QLabel(this)),
      state_label_(new QLabel(tr("Not connected: click Connect"), this)),
      format_label_(new QLabel(this)),
      network_label_(new QLabel(this)),
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
    camera_panel_->setLiveStreaming(true);  // the camera is in Live Streaming mode on the network

    feed_choice_->addItem(tr("Feed: low latency"), int(Feed::LowLatency));
    feed_choice_->addItem(tr("Feed: RTMP"), int(Feed::Rtmp));
    feed_choice_->setToolTip(tr("Low latency: the camera's live view, about 0.15 s behind, 1080p.\n"
                                "RTMP: the camera's RTMP stream through this computer, about 0.4 s behind, "
                                "and its address for other apps (Options → Stream addresses)."));
    feed_choice_->setCurrentIndex(settings_->value(kFeedKey).toString() == QLatin1String("rtmp") ? 1 : 0);
    connect(feed_choice_, &QComboBox::currentIndexChanged, this, [this] {
        settings_->setValue(kFeedKey, feed() == Feed::Rtmp ? QStringLiteral("rtmp") : QStringLiteral("lowLatency"));
        onFeedChosen();
    });

    decoder_choice_->addItem(tr("Decoder: auto (GPU if available)"), int(DecoderPreference::Auto));
    decoder_choice_->addItem(tr("Decoder: GPU"), int(DecoderPreference::Hardware));
    decoder_choice_->addItem(tr("Decoder: CPU"), int(DecoderPreference::Software));
    decoder_choice_->setCurrentIndex(std::max(0, decoder_choice_->findData(settings_->value(kDecoderKey, 0).toInt())));
    connect(decoder_choice_, &QComboBox::currentIndexChanged, this,
            [this] { settings_->setValue(kDecoderKey, decoder_choice_->currentData()); });

    auto* options = new QMenu(tr("Options"), this);
    startup_action_ = add_toggle(options, tr("Connect on startup"), settings_, kStartupKey, false);
    QAction* hold_action = add_toggle(options, tr("Freeze the picture after lost video (instead of showing damaged frames)"),
                                      settings_, kHoldOnLossKey, false);  // real time first
    pipeline_->setHoldOnLoss(hold_action->isChecked());
    connect(hold_action, &QAction::toggled, this, [this](bool on) { pipeline_->setHoldOnLoss(on); });
    options->addSeparator();
    connect(options->addAction(tr("Camera Wi-Fi network...")), &QAction::triggered, this, [this] { askNetwork({}); });
    QMenu* quality_menu = options->addMenu(tr("RTMP quality"));
    auto* qualities = new QActionGroup(quality_menu);
    const int chosen_resolution = rtmp_quality(settings_->value(kRtmpResolutionKey, 1080).toInt()).resolution;
    for (const RtmpQuality& quality : kRtmpQualities) {
        QAction* action = quality_menu->addAction(tr(quality.label));
        action->setCheckable(true);
        action->setChecked(quality.resolution == chosen_resolution);
        qualities->addAction(action);
        connect(action, &QAction::triggered, this, [this, resolution = quality.resolution] {
            settings_->setValue(kRtmpResolutionKey, resolution);
            if (rtmp_requested_) {
                onFeedChosen();  // the camera rejoins and pushes again with the new settings
            }
        });
    }
    connect(options->addAction(tr("Stream addresses...")), &QAction::triggered, this, &MainWindow::showStreamAddresses);
    options->addSeparator();
    connect(options->addAction(tr("Forget the paired camera")), &QAction::triggered, this, &MainWindow::forgetCamera);
    options->addSeparator();
    connect(options->addAction(tr("About DJI VCam")), &QAction::triggered, this, &MainWindow::showAbout);
    auto* options_button = new QToolButton(this);
    options_button->setText(tr("Options"));
    options_button->setMenu(options);
    options_button->setPopupMode(QToolButton::InstantPopup);

    connect_action_->setCheckable(true);
    auto* toolbar = addToolBar(tr("Camera"));
    toolbar->setMovable(false);
    toolbar->addAction(connect_action_);
    toolbar->addSeparator();
    toolbar->addWidget(feed_choice_);
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
    statusBar()->addPermanentWidget(network_label_);
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
    connect(connector_, &CameraConnector::networksFound, this, &MainWindow::onNetworksFound);
    connect(connector_, &CameraConnector::joinFailed, this, &MainWindow::onJoinFailed);
    connect(connector_, &CameraConnector::joined, this, &MainWindow::onJoined);
    connect(connector_, &CameraConnector::linkLost, this, &MainWindow::onLinkLost);
    // On the RTMP feed the camera's settings go over the Bluetooth link.
    connect(connector_, &CameraConnector::cameraChanged, this, [this] {
        const auto state = connector_->takeCameraState();
        if (settings_over_bluetooth_) {
            camera_panel_->showState(state);
        }
    });
    connect(connector_, &CameraConnector::cameraError, this, [this](const QString& message) {
        if (settings_over_bluetooth_) {
            qInfo("camera: %s", qPrintable(message));
            statusBar()->showMessage(tr("Camera: %1").arg(message), 8000);
            camera_panel_->showState(connector_->takeCameraState());  // snap refused values back
        }
    });
    connect(connector_, &CameraConnector::streamStarted, this, [this](bool ok) {
        qInfo("rtmp: the camera %s", ok ? "pushes its RTMP stream" : "refused the RTMP stream");
        if (!ok) {
            showStage(tr("The camera refused to start its RTMP stream: try the low-latency feed"), true);
        }
    });
    connect(connector_, &CameraConnector::cameraFound, this, [this](const QString& name, const QString& address, int model) {
        const auto id = static_cast<std::uint8_t>(model);
        const QString model_name = QString::fromStdString(djivcam::camera::model_name(id));
        camera_label_->setText(model_name.isEmpty() ? tr("Camera: %1").arg(name) : tr("Camera: %1 (%2)").arg(model_name, name));
        camera_label_->setToolTip(tr("Bluetooth name %1, DJI model byte 0x%2").arg(name).arg(model, 2, 16, QLatin1Char('0')));
        qInfo("bluetooth: found %s, model 0x%02x (%s)", qPrintable(name), model,
              model_name.isEmpty() ? "unknown" : qPrintable(model_name));
        if (!djivcam::camera::model_tested(id)) {
            statusBar()->showMessage(tr("%1 has not been tested with DJI VCam yet (only the Osmo Action 5 Pro has): it may not work")
                                         .arg(model_name.isEmpty() ? tr("This DJI camera") : model_name),
                                     20000);
        }
        settings_->setValue(kAddressKey, address);
    });
    if (rtmp_) {
        connect(rtmp_, &RtmpServer::publisherChanged, this, [this](const QString& from) {
            statusBar()->showMessage(from.isEmpty() ? tr("The camera's RTMP stream stopped")
                                                    : tr("The camera pushes its RTMP stream from %1").arg(from),
                                     6000);
        });
        connect(rtmp_, &RtmpServer::failed, this, [this](const QString& message) {
            qInfo("rtmp: %s", qPrintable(message));
            if (feed() == Feed::Rtmp && connect_action_->isChecked()) {
                showStage(message, true);
            }
        });
    }

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
    updateNetworkLabel();
    showStage(state_label_->text());

    if (startup_action_->isChecked()) {
        QTimer::singleShot(0, this, &MainWindow::connectCamera);
    }
}

MainWindow::~MainWindow() {
    finder_ = {};  // stops a lookup still running
    connector_->stop();
    camera_panel_->setController(nullptr);  // the controller goes away with the pipeline's session
    pipeline_->stop();
    if (rtmp_) {
        rtmp_->stop();
    }
#ifdef DJIVCAM_HAVE_VCAM
    delete virtual_camera_;
#endif
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
    // The installer registers the webcam for all users; a portable copy registers it once, for good,
    // for this user (no administrator needed).
    if (!VirtualCamera::camera_registered() && !VirtualCamera::register_camera(false, &error)) {
        QMessageBox::warning(this, tr("Virtual camera"), QString::fromStdString(error));
        QSignalBlocker block(vcam_action_);
        vcam_action_->setChecked(false);
        updateVirtualCameraStatus();
        return;
    }
    virtual_camera_->start();
    settings_->setValue(kVirtualCameraKey, true);
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

void MainWindow::connectCamera() { connect_action_->setChecked(true); }

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("About DJI VCam"),
                       tr("<p><b>DJI VCam</b> %1</p>"
                          "<p>A DJI Osmo Action camera's live view as a wireless webcam.</p>"
                          "<p><a href=\"https://github.com/RB14/dji-vcam\">github.com/RB14/dji-vcam</a> · MIT License</p>"
                          "<p>DJI, Osmo and Mimo are trademarks of SZ DJI Technology Co., Ltd. "
                          "This project is not affiliated with or endorsed by DJI.</p>")
                           .arg(QApplication::applicationVersion().toHtmlEscaped()));
}

void MainWindow::replayFile(const QString& path) {
    replay_file_ = path;
    camera_label_->setText(tr("Replay: %1").arg(QFileInfo(path).fileName()));
    connectCamera();
}

void MainWindow::playStream(const QString& url) {
    stream_url_ = url;
    camera_label_->setText(tr("Stream: %1").arg(url));
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

QString MainWindow::networkPassword() const { return secret::unprotect(settings_->value(kNetworkPasswordKey).toByteArray()); }

MainWindow::Feed MainWindow::feed() const { return static_cast<Feed>(feed_choice_->currentData().toInt()); }

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
        finder_ = {};
        connector_->stop();  // back to Video mode: the camera returns to its own access point
        stopFeed();
        if (rtmp_) {
            rtmp_->stop();
        }
        joined_ssid_.clear();
        camera_ip_.clear();
        rtmp_requested_ = false;
        updateNetworkLabel();
        showStatusView();
        showStage(tr("Not connected: click Connect"));
        decoder_label_->clear();
        decoder_label_->setToolTip({});
        return;
    }
    const auto decoder = static_cast<DecoderPreference>(decoder_choice_->currentData().toInt());
    if (!replay_file_.isEmpty()) {
        camera_panel_->setController(nullptr);
        pipeline_->startReplay(replay_file_, decoder);
        return;
    }
    if (!stream_url_.isEmpty()) {
        camera_panel_->setController(nullptr);
        pipeline_->startStream(stream_url_, decoder);
        return;
    }
    showStatusView();
    if (const QString direct = settings_->value(kCameraIpKey).toString(); !direct.isEmpty()) {
        camera_ip_ = direct;  // advanced: the camera is already reachable there
        startLowLatency();
        return;
    }
    if (!CameraConnector::bluetoothAvailable()) {
        showStage(tr("Bluetooth is off or missing: DJI VCam needs it to put the camera on your Wi-Fi"), true);
        return;
    }
    connector_->start(pairingIdentifier(), kPairingToken, settings_->value(kAddressKey).toString(),
                      settings_->value(kNetworkKey).toString(), networkPassword());
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

void MainWindow::updateNetworkLabel() {
    const QString network = joined_ssid_.isEmpty() ? settings_->value(kNetworkKey).toString() : joined_ssid_;
    QString text;
    if (!network.isEmpty()) {
        text = camera_ip_.isEmpty() ? tr("Wi-Fi: %1").arg(network) : tr("Wi-Fi: %1 · %2").arg(network, camera_ip_);
    }
    network_label_->setText(text);
    network_label_->setToolTip(tr("The Wi-Fi network the camera joins, and its address there "
                                  "(Options → Camera Wi-Fi network)"));
}

void MainWindow::onConnectorStage(Stage stage, const QString& detail) {
    qInfo("bluetooth: %s", qPrintable(detail));
    if (!streaming_ && stage != Stage::Idle) {
        showStage(detail, stage == Stage::ApprovalNeeded || stage == Stage::Failed || stage == Stage::NeedNetwork);
    }
}

void MainWindow::askNetwork(const QString& error) {
    if (!network_dialog_) {
        network_dialog_ = new NetworkDialog(settings_->value(kNetworkKey).toString(), networkPassword(), this);
        network_dialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(network_dialog_, &NetworkDialog::rescanRequested, connector_, &CameraConnector::rescan);
        connect(network_dialog_, &QDialog::accepted, this, [this] {
            const QString ssid = network_dialog_->ssid();
            const QString password = network_dialog_->password();
            settings_->setValue(kNetworkKey, ssid);
            settings_->setValue(kNetworkPasswordKey, secret::protect(password));
            updateNetworkLabel();
            if (connect_action_->isChecked() && ssid != joined_ssid_) {
                stopFeed();
                joined_ssid_.clear();
                camera_ip_.clear();
                rtmp_requested_ = false;
                connector_->setNetwork(ssid, password);  // joins, or leaves the current network for it
            }
        });
        connect(network_dialog_, &QDialog::rejected, this, [this] {
            if (connect_action_->isChecked() && joined_ssid_.isEmpty()) {
                connect_action_->setChecked(false);  // the camera waits for a network: give up
            }
        });
        if (!connect_action_->isChecked()) {
            network_dialog_->setOffline();
        }
    }
    network_dialog_->setError(error);
    network_dialog_->show();
    network_dialog_->raise();
    network_dialog_->activateWindow();
}

void MainWindow::onNetworksFound(const QList<CameraNetwork>& networks) {
    qInfo("bluetooth: the camera hears %lld networks", static_cast<long long>(networks.size()));
    if (!network_dialog_ && joined_ssid_.isEmpty()) {
        askNetwork({});  // no network chosen yet: the connector scanned to ask for one
    }
    if (network_dialog_) {
        network_dialog_->setNetworks(networks);
    }
}

void MainWindow::onJoinFailed(const QString& ssid) {
    qInfo("bluetooth: the camera could not join %s", qPrintable(ssid));
    showStage(tr("The camera could not join %1").arg(ssid), true);
    askNetwork(tr("The camera could not join %1. Check the password, and that the camera can reach this "
                  "network (2.4 GHz, unless the camera's Wi-Fi band is set to 5 GHz).")
                   .arg(ssid));
    connector_->rescan();
}

void MainWindow::onJoined(const QString& ssid, const QString& mac) {
    joined_ssid_ = ssid;
    camera_mac_ = mac;
    camera_ip_.clear();
    rtmp_requested_ = false;
    updateNetworkLabel();
    if (network_dialog_) {
        network_dialog_->close();
    }
    showStage(tr("The camera is on %1: looking for it there").arg(ssid));
    finder_ = std::jthread([this, mac = mac.toStdString()](std::stop_token stop) {
        const auto ip = djivcam::net::find_ip_by_mac(mac, kFindCameraTimeout, stop);
        if (!stop.stop_requested()) {
            QMetaObject::invokeMethod(this, [this, ip = QString::fromStdString(ip.value_or(std::string()))] { onCameraAddress(ip); },
                                      Qt::QueuedConnection);
        }
    });
}

void MainWindow::onCameraAddress(const QString& ip) {
    if (joined_ssid_.isEmpty() || !connect_action_->isChecked()) {
        return;  // meanwhile disconnected or the link was lost
    }
    if (ip.isEmpty()) {
        qInfo("network: camera %s not found on this computer's networks", qPrintable(camera_mac_));
        showStage(tr("The camera is on %1, but this computer does not see it there. Is this computer on the same "
                     "network (and not a guest network that keeps its devices apart)?")
                      .arg(joined_ssid_),
                  true);
        return;
    }
    qInfo("network: the camera is at %s", qPrintable(ip));
    camera_ip_ = ip;
    updateNetworkLabel();
    startFeed();
}

void MainWindow::onLinkLost() {
    finder_ = {};
    stopFeed();
    joined_ssid_.clear();
    camera_ip_.clear();
    rtmp_requested_ = false;
    updateNetworkLabel();
    showStatusView();
}

void MainWindow::startFeed() {
    if (feed() == Feed::Rtmp) {
        startRtmp();
    } else {
        startLowLatency();
    }
}

void MainWindow::startLowLatency() {
    djivcam::SessionConfig config;
    config.identifier = pairingIdentifier().toStdString();
    config.token = kPairingToken.toStdString();
    config.camera_ip = camera_ip_.toStdString();
    camera_panel_->setController(nullptr);
    camera_panel_->setUnavailable({});
    settings_over_bluetooth_ = false;
    connector_->setCameraControl(false);  // the live view's connection carries them
    pipeline_->start(config, static_cast<DecoderPreference>(decoder_choice_->currentData().toInt()));
    camera_panel_->setParametersOnly(false);  // the live view's connection carries every setting
    camera_panel_->setController(pipeline_->camera());
}

void MainWindow::startRtmp() {
    if (!rtmp_) {
        showStage(tr("The RTMP feed needs go2rtc.exe next to DJI VCam: reinstall the app, or use the low-latency feed"), true);
        return;
    }
    QString error;
    if (!rtmp_->start(&error)) {
        showStage(tr("The RTMP server did not start: %1").arg(error), true);
        return;
    }
    // The camera reaches this computer at its address on the camera's network.
    const auto host = djivcam::net::local_ip_towards(camera_ip_.toStdString(), RtmpServer::kRtmpPort);
    if (!host) {
        showStage(tr("This computer has no address on the camera's network"), true);
        return;
    }
    camera_panel_->setController(nullptr);
    enableBluetoothSettings();
    if (!rtmp_requested_) {
        const RtmpQuality& quality = rtmp_quality(settings_->value(kRtmpResolutionKey, 1080).toInt());
        const QString local = QString::fromStdString(*host);
        djivcam::live::StreamSettings stream{quality.code, quality.kbps, rtmp_->ingestUrl(local).toStdString()};
        if (!djivcam::live::fits(stream)) {
            stream.url = rtmp_->ingestUrl(local, false).toStdString();  // the camera takes ~35 characters
        }
        if (!djivcam::live::fits(stream)) {
            showStage(tr("This computer's address is too long for the camera's RTMP settings"), true);
            return;
        }
        qInfo("rtmp: asking the camera to push %dp at %u kbit/s to %s", quality.resolution, quality.kbps, stream.url.c_str());
        connector_->startStream(stream);
        rtmp_requested_ = true;
    }
    showStage(tr("Starting the camera's RTMP stream"));
    pipeline_->startStream(rtmp_->playbackUrl(), static_cast<DecoderPreference>(decoder_choice_->currentData().toInt()));
}

void MainWindow::enableBluetoothSettings() {  // the RTMP feed
    camera_panel_->setUnavailable({});
    camera_panel_->setParametersOnly(true);
    settings_over_bluetooth_ = true;
    connector_->setCameraControl(true);
    camera_panel_->setController(connector_->camera());
}

void MainWindow::stopFeed() {
    camera_panel_->setController(nullptr);
    settings_over_bluetooth_ = false;
    connector_->setCameraControl(false);
    pipeline_->stop();
    streaming_ = false;
    format_label_->clear();
    stats_label_->clear();
}

void MainWindow::onFeedChosen() {
    if (!connect_action_->isChecked() || joined_ssid_.isEmpty() || camera_ip_.isEmpty()) {
        return;  // the choice applies once the camera is on the network
    }
    stopFeed();
    showStatusView();
    if (rtmp_requested_) {
        // Only leaving the network ends the camera's push: it rejoins, then the chosen feed starts.
        qInfo("rtmp: the camera rejoins %s without its push", qPrintable(joined_ssid_));
        const QString ssid = joined_ssid_;
        joined_ssid_.clear();
        camera_ip_.clear();
        rtmp_requested_ = false;
        if (rtmp_) {
            rtmp_->stop();
        }
        connector_->setNetwork(ssid, networkPassword());
        return;
    }
    startFeed();
}

void MainWindow::showStreamAddresses() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Stream addresses"));
    auto* layout = new QVBoxLayout(&dialog);
    if (!rtmp_) {
        layout->addWidget(new QLabel(tr("This copy of DJI VCam has no RTMP server (go2rtc.exe)."), &dialog));
    } else {
        const auto host = camera_ip_.isEmpty() ? std::nullopt
                                               : djivcam::net::local_ip_towards(camera_ip_.toStdString(), RtmpServer::kRtmpPort);
        auto* intro = new QLabel(tr("With the RTMP feed (Feed: RTMP), the camera pushes its stream to this computer, "
                                    "and other apps can open it too."),
                                 &dialog);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto* grid = new QGridLayout;
        int row = 0;
        auto add = [&](const QString& label, const QString& url) {
            auto* field = new QLineEdit(url, &dialog);
            field->setReadOnly(true);
            field->setMinimumWidth(360);
            auto* copy = new QPushButton(tr("Copy"), &dialog);
            QObject::connect(copy, &QPushButton::clicked, &dialog, [url] { QGuiApplication::clipboard()->setText(url); });
            grid->addWidget(new QLabel(label, &dialog), row, 0);
            grid->addWidget(field, row, 1);
            grid->addWidget(copy, row, 2);
            ++row;
        };
        add(tr("The camera pushes to"), host ? rtmp_->ingestUrl(QString::fromStdString(*host))
                                             : rtmp_->ingestUrl(tr("<this computer>")));
        const QStringList shared = rtmp_->shareUrls();
        for (const QString& url : shared) {
            add(tr("Other apps (OBS, VLC)"), url);
        }
        layout->addLayout(grid);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::onSessionState(const QString& state, const QString& detail) {
    qInfo("session: %s%s", qPrintable(state), detail.isEmpty() ? "" : qPrintable(" (" + detail + ")"));
    const bool was_streaming = streaming_;
    streaming_ = state == QLatin1String("streaming");
    if (was_streaming && !streaming_) {
        showStatusView();  // no frozen picture: show what the app is doing instead
    }
    const QString suffix = detail.isEmpty() ? QString() : " (" + detail + ")";
    if (state == QLatin1String("waiting for camera network")) {
        showStage(tr("Waiting for the camera on the network%1").arg(suffix));
    } else if (state == QLatin1String("waiting for the RTMP feed")) {
        showStage(tr("Waiting for the camera's RTMP stream%1").arg(suffix));
    } else if (state == QLatin1String("connecting")) {
        showStage(tr("Connecting to the camera%1").arg(suffix));
    } else if (streaming_) {
        showStage(detail.isEmpty() ? tr("Streaming") : tr("Streaming (%1)").arg(detail));
    }
}

void MainWindow::onStats(const LiveStats& stats) {
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

void MainWindow::onDecoder(const QString& backend, const QString& gpu, bool hardware) {
    qInfo("decoder: %s%s", qPrintable(backend), gpu.isEmpty() ? "" : qPrintable(QStringLiteral(" on ") + gpu));
    if (!hardware) {
        decoder_label_->setText(tr("decoder: CPU"));
        decoder_label_->setToolTip(tr("Software decoding (FFmpeg)"));
        return;
    }
    decoder_label_->setText(tr("decoder: GPU (%1)").arg(gpu.isEmpty() ? backend : gpu));
    decoder_label_->setToolTip(tr("Hardware decoding with FFmpeg's %1").arg(backend));
}
