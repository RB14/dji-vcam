#include "main_window.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include "pipeline.h"
#include "preview_widget.h"

using djivcam::media::DecoderPreference;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      pipeline_(new Pipeline(this)),
      preview_(new PreviewWidget(this)),
      connect_action_(new QAction(tr("Connect"), this)),
      decoder_choice_(new QComboBox(this)),
      state_label_(new QLabel(tr("Not connected"), this)),
      format_label_(new QLabel(this)),
      stats_label_(new QLabel(this)),
      decoder_label_(new QLabel(this)) {
    setWindowTitle(tr("DJI VCam - DJI Osmo Action live view"));
    setCentralWidget(preview_);
    resize(1280, 800);

    decoder_choice_->addItem(tr("Decoder: auto (GPU if available)"), QVariant::fromValue(int(DecoderPreference::Auto)));
    decoder_choice_->addItem(tr("Decoder: GPU"), QVariant::fromValue(int(DecoderPreference::Hardware)));
    decoder_choice_->addItem(tr("Decoder: CPU"), QVariant::fromValue(int(DecoderPreference::Software)));

    connect_action_->setCheckable(true);
    auto* toolbar = addToolBar(tr("Camera"));
    toolbar->setMovable(false);
    toolbar->addAction(connect_action_);
    toolbar->addSeparator();
    toolbar->addWidget(decoder_choice_);

    statusBar()->addWidget(state_label_, 1);
    statusBar()->addPermanentWidget(format_label_);
    statusBar()->addPermanentWidget(stats_label_);
    statusBar()->addPermanentWidget(decoder_label_);

    connect(connect_action_, &QAction::toggled, this, &MainWindow::toggleConnection);
    connect(pipeline_, &Pipeline::frameAvailable, this, [this] {
        if (QImage frame = pipeline_->takeLatestFrame(); !frame.isNull()) {
            preview_->showFrame(frame);
        }
    });
    connect(pipeline_, &Pipeline::stateChanged, this, &MainWindow::onStateChanged);
    connect(pipeline_, &Pipeline::statsUpdated, this, &MainWindow::onStats);
    connect(pipeline_, &Pipeline::decoderChanged, this, &MainWindow::onDecoder);
    connect(pipeline_, &Pipeline::formatChanged, this, [this](int width, int height) {
        format_label_->setText(tr("%1\u00d7%2").arg(width).arg(height));
    });
    connect(pipeline_, &Pipeline::errorOccurred, this, [this](const QString& message) {
        QMessageBox::warning(this, tr("Decoder error"), message);
        connect_action_->setChecked(false);
    });
}

MainWindow::~MainWindow() { pipeline_->stop(); }

void MainWindow::connectCamera() { connect_action_->setChecked(true); }

void MainWindow::toggleConnection(bool connect) {
    connect_action_->setText(connect ? tr("Disconnect") : tr("Connect"));
    decoder_choice_->setEnabled(!connect);
    if (connect) {
        const auto decoder = static_cast<DecoderPreference>(decoder_choice_->currentData().toInt());
        djivcam::SessionConfig config;
        if (!identifier_.isEmpty()) {
            config.identifier = identifier_.toStdString();
        }
        pipeline_->start(config, decoder);
    } else {
        pipeline_->stop();
        preview_->clear();
        state_label_->setText(tr("Not connected"));
        format_label_->clear();
        stats_label_->clear();
        decoder_label_->clear();
    }
}

void MainWindow::onStateChanged(const QString& state, const QString& detail) {
    // Friendly, capitalized stage text; the session's detail (e.g. "no video, reconnecting") after it.
    QString stage = state;
    if (!stage.isEmpty()) {
        stage[0] = stage[0].toUpper();
    }
    if (state == QLatin1String("waiting for camera network")) {
        stage = tr("Waiting for the camera network (is the camera awake and the bridge joined?)");
    }
    state_label_->setText(detail.isEmpty() ? stage : QStringLiteral("%1: %2").arg(stage, detail));
}

void MainWindow::onStats(double fps, double kbps, double loss_percent, quint64 recovered, quint64 reconnects) {
    stats_label_->setText(tr("%1 fps  |  %2 kbit/s  |  loss %3%  |  %4 recovered  |  %5 reconnects")
                              .arg(fps, 0, 'f', 0)
                              .arg(kbps, 0, 'f', 0)
                              .arg(loss_percent, 0, 'f', 2)
                              .arg(recovered)
                              .arg(reconnects));
}

void MainWindow::onDecoder(const QString& backend, bool hardware) {
    decoder_label_->setText(tr("decoder: %1 (%2)").arg(backend, hardware ? tr("GPU") : tr("CPU")));
}
