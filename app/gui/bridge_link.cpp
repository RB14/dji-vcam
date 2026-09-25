#include "bridge_link.h"

#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>

namespace {

constexpr quint16 kBridgeVendorId = 0x303A;
constexpr quint16 kBridgeProductId = 0x4001;
constexpr int kReplyTimeoutMs = 1500;
constexpr int kScanTimeoutMs = 8000;

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

std::optional<std::vector<djivcam::wifi::Network>> BridgeLink::scan(QString* error) {
    const auto port_name = findPort();
    if (!port_name) {
        *error = QObject::tr("ESP32 bridge not found on USB");
        return std::nullopt;
    }
    QSerialPort port(*port_name);
    port.setBaudRate(QSerialPort::Baud115200);
    if (!port.open(QIODevice::ReadWrite)) {
        *error = QObject::tr("cannot open %1: %2").arg(*port_name, port.errorString());
        return std::nullopt;
    }
    port.setDataTerminalReady(true);
    port.readAll();
    port.write("scan\n");
    port.waitForBytesWritten(kReplyTimeoutMs);
    // "wifi: scan: N networks", then one line per network:
    // "wifi:    -59 dBm  ch  4  wpa2       02:00:00:00:00:01  'net-a'"
    static const QRegularExpression count_line(QStringLiteral("scan: (\\d+) networks"));
    static const QRegularExpression network_line(QStringLiteral("(-?\\d+) dBm\\s+ch\\s+(\\d+)\\s+\\S+\\s+[0-9a-f:]{17}\\s+'([^']*)'"));
    QString text;
    std::optional<int> expected;
    std::vector<djivcam::wifi::Network> networks;
    for (int waited = 0; waited < kScanTimeoutMs; waited += 100) {
        if (port.waitForReadyRead(100)) {
            text += QString::fromUtf8(port.readAll());
        }
        if (!expected) {
            if (const auto count = count_line.match(text); count.hasMatch()) {
                expected = count.captured(1).toInt();
            }
        }
        networks.clear();
        for (auto it = network_line.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            networks.push_back({match.captured(2).toInt(), match.captured(1).toInt(), match.captured(3).toStdString()});
        }
        if (expected && static_cast<int>(networks.size()) >= *expected) {
            return networks;
        }
    }
    *error = QObject::tr("the bridge on %1 did not finish its scan").arg(*port_name);
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
