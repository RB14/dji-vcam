// Main window: live preview, connect/disconnect, decoder choice and live statistics.
#pragma once

#include <QMainWindow>

class QAction;
class QComboBox;
class QLabel;
class Pipeline;
class PreviewWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void connectCamera();
    // Identifier used for the datalink's TCP 7001 poke; it must be one the camera approved.
    void setPairingIdentifier(const QString& identifier) { identifier_ = identifier; }

private:
    void toggleConnection(bool connect);
    void onStateChanged(const QString& state, const QString& detail);
    void onStats(double fps, double kbps, double loss_percent, quint64 recovered, quint64 reconnects);
    void onDecoder(const QString& backend, bool hardware);

    QString identifier_;
    Pipeline* pipeline_;
    PreviewWidget* preview_;
    QAction* connect_action_;
    QComboBox* decoder_choice_;
    QLabel* state_label_;
    QLabel* format_label_;
    QLabel* stats_label_;
    QLabel* decoder_label_;
};
