#include "go2rtc_server.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QString Go2RtcServer::parsePublisher(const QByteArray& reply) const {
    // {"dji-vcam":{"producers":[{"format_name":"rtmp","remote_addr":"192.168.1.5:54321",...}],...}}
    const QJsonArray producers =
        QJsonDocument::fromJson(reply).object().value(streamName()).toObject().value(QStringLiteral("producers")).toArray();
    for (const QJsonValue& producer : producers) {
        const QString address = producer.toObject().value(QStringLiteral("remote_addr")).toString();
        if (!address.isEmpty()) {
            return address.section(QLatin1Char(':'), 0, 0);  // the host of "192.168.1.5:54321"
        }
    }
    return {};
}
