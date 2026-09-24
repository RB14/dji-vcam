#include "bridge_link.h"

#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>

namespace {

constexpr quint16 kBridgeVendorId = 0x303A;
constexpr quint16 kBridgeProductId = 0x4001;
constexpr int kReplyTimeoutMs = 1500;

// Sends one console command and collects the output until `until` appears or the timeout.
QString command(QSerialPort& port, const QByteArray& line, const QString& until) {
    port.readAll();
    port.write(line + '\n');
    port.waitForBytesWritten(kReplyTimeoutMs);
    QString reply;
    for (int waited = 0; waited < kReplyTimeoutMs && !reply.contains(until); waited += 100) {
        if (port.waitForReadyRead(100)) {
            reply += QString::fromUtf8(port.readAll());
        }
    }
    return reply;
}

QByteArray quoted(const QString& text) {
    return '"' + text.toUtf8() + '"';
}

}  // namespace

std::optional<QString> BridgeLink::findPort() {
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        if (info.hasVendorIdentifier() && info.vendorIdentifier() == kBridgeVendorId &&
            info.hasProductIdentifier() && info.productIdentifier() == kBridgeProductId) {
            return info.portName();
        }
    }
    return std::nullopt;
}

bool BridgeLink::ensureCredentials(const QString& ssid, const QString& password, QString* error) {
    const auto port_name = findPort();
    if (!port_name) {
        *error = QObject::tr("ESP32 bridge not found on USB");
        return false;
    }
    QSerialPort port(*port_name);
    port.setBaudRate(QSerialPort::Baud115200);
    if (!port.open(QIODevice::ReadWrite)) {
        *error = QObject::tr("cannot open %1: %2").arg(*port_name, port.errorString());
        return false;
    }
    port.setDataTerminalReady(true);  // the console only talks to an open terminal

    const QString status = command(port, "status", QStringLiteral("heap:"));
    const auto current = QRegularExpression(QStringLiteral("ssid='([^']*)'")).match(status);
    if (!current.hasMatch()) {
        *error = QObject::tr("the bridge on %1 did not answer").arg(*port_name);
        return false;
    }
    if (current.captured(1) == ssid) {
        return true;  // already configured; it (re)joins by itself
    }
    const QString reply = command(port, "wifi " + quoted(ssid) + ' ' + quoted(password), QStringLiteral("wifi: "));
    if (!reply.contains(QStringLiteral("ESP_OK"))) {
        *error = QObject::tr("the bridge rejected the credentials");
        return false;
    }
    return true;
}
