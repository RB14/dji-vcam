// Asks which Wi-Fi network the camera joins, and its password. Lists the networks the camera hears
// (plus the one this computer is on, which it must share with the camera) and takes a hidden network
// by name.
#pragma once

#include <QDialog>
#include <QList>

#include "camera_connector.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class NetworkDialog : public QDialog {
    Q_OBJECT

public:
    // `current`: the network chosen before (preselected, with its password).
    NetworkDialog(const QString& current, const QString& password, QWidget* parent = nullptr);

    void setNetworks(const QList<CameraNetwork>& networks);
    // Not connected: the camera cannot scan now (a network can still be typed or picked).
    void setOffline();
    // Why the last choice did not work (e.g. the camera could not join it).
    void setError(const QString& message);

    QString ssid() const;
    QString password() const;

signals:
    void rescanRequested();

private:
    void choose(const QString& ssid);
    void updateOk();

    QString this_computer_;  // this computer's Wi-Fi network
    QListWidget* list_;
    QLineEdit* name_;
    QLineEdit* password_;
    QLabel* error_;
    QPushButton* ok_;
    QPushButton* rescan_;
};
