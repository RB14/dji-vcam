#include "network_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

#include "wifi_info.h"

namespace {

constexpr int kSsidRole = Qt::UserRole;

}  // namespace

NetworkDialog::NetworkDialog(const QString& current, const QString& password, QWidget* parent)
    : QDialog(parent),
      this_computer_(currentWifiSsid()),
      list_(new QListWidget(this)),
      name_(new QLineEdit(this)),
      password_(new QLineEdit(this)),
      error_(new QLabel(this)),
      ok_(nullptr),
      rescan_(new QPushButton(tr("Rescan"), this)) {
    setWindowTitle(tr("Camera Wi-Fi network"));
    auto* intro = new QLabel(tr("The camera joins a Wi-Fi network, which this computer must be on too%1.")
                                 .arg(this_computer_.isEmpty() ? QString() : tr(" (it is on <b>%1</b>)").arg(this_computer_.toHtmlEscaped())),
                             this);
    intro->setWordWrap(true);
    list_->setMinimumHeight(180);
    list_->addItem(tr("Asking the camera which networks it hears..."));
    list_->item(0)->setFlags(Qt::NoItemFlags);
    name_->setPlaceholderText(tr("Network name (for a hidden network, type it)"));
    name_->setMaxLength(32);
    password_->setEchoMode(QLineEdit::Password);
    password_->setMaxLength(63);
    auto* show = new QCheckBox(tr("Show"), this);
    connect(show, &QCheckBox::toggled, this, [this](bool on) { password_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    auto* password_row = new QHBoxLayout;
    password_row->addWidget(password_, 1);
    password_row->addWidget(show);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #d97706; font-weight: bold;"));
    error_->hide();

    auto* form = new QFormLayout;
    form->addRow(tr("Network"), name_);
    form->addRow(tr("Password"), password_row);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(tr("Join"));
    buttons->addButton(rescan_, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(list_, 1);
    layout->addLayout(form);
    layout->addWidget(error_);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(rescan_, &QPushButton::clicked, this, [this] {
        rescan_->setEnabled(false);
        emit rescanRequested();
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item && item->flags() != Qt::NoItemFlags) {
            name_->setText(item->data(kSsidRole).toString());
            password_->setFocus();
        }
    });
    connect(name_, &QLineEdit::textChanged, this, &NetworkDialog::updateOk);
    name_->setText(current.isEmpty() ? this_computer_ : current);
    password_->setText(current.isEmpty() ? QString() : password);
    updateOk();
}

void NetworkDialog::setNetworks(const QList<CameraNetwork>& networks) {
    rescan_->setEnabled(true);
    list_->clear();
    QSet<QString> seen;
    QList<CameraNetwork> ordered;
    for (const CameraNetwork& network : networks) {  // this computer's network first, each name once
        if (!seen.contains(network.ssid)) {
            seen.insert(network.ssid);
            network.ssid == this_computer_ ? ordered.prepend(network) : ordered.append(network);
        }
    }
    if (!this_computer_.isEmpty() && !seen.contains(this_computer_)) {
        ordered.prepend({this_computer_, false});  // not heard (e.g. hidden): the camera may still join it
    }
    for (const CameraNetwork& network : ordered) {
        QString label = network.ssid;
        if (network.fiveGhz) {
            label += tr("  (5 GHz)");
        }
        if (network.ssid == this_computer_) {
            label += tr("  (this computer's network)");
        }
        auto* item = new QListWidgetItem(label, list_);
        item->setData(kSsidRole, network.ssid);
    }
    if (ordered.isEmpty()) {
        list_->addItem(tr("The camera heard no networks: type the name below, or Rescan"));
        list_->item(0)->setFlags(Qt::NoItemFlags);
    }
    choose(name_->text());
}

void NetworkDialog::setOffline() {
    setNetworks({});
    rescan_->setEnabled(false);
    if (list_->count() && list_->item(list_->count() - 1)->flags() == Qt::NoItemFlags) {
        list_->item(list_->count() - 1)->setText(tr("Connect to see the networks the camera hears"));
    }
}

void NetworkDialog::setError(const QString& message) {
    error_->setText(message);
    error_->setVisible(!message.isEmpty());
}

QString NetworkDialog::ssid() const { return name_->text().trimmed(); }

QString NetworkDialog::password() const { return password_->text(); }

void NetworkDialog::choose(const QString& ssid) {
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->data(kSsidRole).toString() == ssid) {
            const QSignalBlocker quiet(list_);  // keep the typed password
            list_->setCurrentRow(row);
            return;
        }
    }
}

void NetworkDialog::updateOk() {
    const QByteArray name = ssid().toUtf8();
    ok_->setEnabled(!name.isEmpty() && name.size() <= 32);
}
