// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huediscovery.h"

#include "huemdns.h"
#include "huetls.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUdpSocket>

#include <algorithm>

HueDiscovery::HueDiscovery(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
    , m_cloudUrl(QStringLiteral("https://discovery.meethue.com/"))
{
    // mDNS answers can get lost; ask a few times.
    m_queryTimer.setInterval(1500);
    connect(&m_queryTimer, &QTimer::timeout, this, &HueDiscovery::sendMdnsQuery);
    m_stopTimer.setSingleShot(true);
    m_stopTimer.setInterval(8000);
    connect(&m_stopTimer, &QTimer::timeout, this, &HueDiscovery::stop);
}

QUrl HueDiscovery::baseUrlFor(const QString &host)
{
    const QString trimmed = host.trimmed();
    QUrl url(trimmed.contains(QLatin1String("://")) ? trimmed : QStringLiteral("https://") + trimmed);
    url.setPath(QString());
    return url;
}

void HueDiscovery::start()
{
    stop();
    m_probed.clear();
    setRunning(true);
    m_stopTimer.start();

    if (m_mdnsEnabled) {
        // Sent from an ephemeral port, so responders answer us directly (RFC 6762 6.7) and we
        // don't need to share port 5353 with anything else on the phone.
        m_socket = new QUdpSocket(this);
        if (m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
            m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 255);
            connect(m_socket, &QUdpSocket::readyRead, this, &HueDiscovery::readMdns);
            m_queriesSent = 0;
            sendMdnsQuery();
            m_queryTimer.start();
        }
    }
    if (m_cloudUrl.isValid())
        queryCloud();
}

void HueDiscovery::stop()
{
    m_queryTimer.stop();
    m_stopTimer.stop();
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    setRunning(m_probesRunning > 0);
}

void HueDiscovery::sendMdnsQuery()
{
    if (!m_socket || ++m_queriesSent > 3) {
        m_queryTimer.stop();
        return;
    }
    m_socket->writeDatagram(HueMdns::buildQuery(), QHostAddress(QStringLiteral("224.0.0.251")), 5353);
}

void HueDiscovery::readMdns()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        m_socket->readDatagram(datagram.data(), datagram.size(), &sender);

        QList<HueMdns::Record> records;
        HueMdns::parse(datagram, &records);
        HueMdns::Bridge bridge;
        if (!HueMdns::findBridge(records, &bridge))
            continue;
        // No A record: the bridge answered for itself, so it's the sender.
        QHostAddress address = bridge.address.isNull() ? sender : bridge.address;
        if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            bool ok = false;
            const quint32 v4 = address.toIPv4Address(&ok);
            if (!ok)
                continue;
            address = QHostAddress(v4);
        }
        QString host = address.toString();
        if (bridge.port != 443 && bridge.port != 0)
            host += QLatin1Char(':') + QString::number(bridge.port);
        probe(host);
    }
}

void HueDiscovery::queryCloud()
{
    QNetworkReply *reply = m_nam->get(QNetworkRequest(m_cloudUrl));
    ++m_probesRunning;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        --m_probesRunning;
        // Rate limited (429) or offline: mDNS and typing in an address still work.
        const QJsonArray list = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &value : list) {
            const QJsonObject entry = value.toObject();
            QString host = entry.value(QStringLiteral("internalipaddress")).toString();
            const int port = entry.value(QStringLiteral("port")).toInt(443);
            if (host.isEmpty())
                continue;
            if (port != 443)
                host += QLatin1Char(':') + QString::number(port);
            probe(host);
        }
        if (!m_stopTimer.isActive())
            setRunning(m_probesRunning > 0);
    });
}

void HueDiscovery::probe(const QString &host)
{
    if (m_probed.contains(host))
        return;
    m_probed.insert(host);

    QUrl url = baseUrlFor(host);
    url.setPath(QStringLiteral("/api/0/config"));
    QNetworkRequest request(url);
    QNetworkReply *reply = m_nam->get(request);
    // Only asking who it is: any certificate will do. Pairing pins the one to trust.
    HueTls::attach(reply, QByteArray());
    ++m_probesRunning;
    setRunning(true);
    QTimer::singleShot(8000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, host]() {
        reply->deleteLater();
        --m_probesRunning;
        const QJsonObject config = QJsonDocument::fromJson(reply->readAll()).object();
        const QString id = config.value(QStringLiteral("bridgeid")).toString().toLower();
        if (reply->error() != QNetworkReply::NoError || id.isEmpty()) {
            m_probed.remove(host); // may work next time
            emit probeFinished(host, false, reply->error() != QNetworkReply::NoError
                               ? reply->errorString() : tr("This is not a Hue bridge"));
        } else {
            const QString model = config.value(QStringLiteral("modelid")).toString();
            QVariantMap bridge;
            bridge.insert(QStringLiteral("id"), id);
            bridge.insert(QStringLiteral("host"), host);
            bridge.insert(QStringLiteral("name"), config.value(QStringLiteral("name")).toString());
            bridge.insert(QStringLiteral("modelId"), model);
            // The first, round bridge (BSB001) never got the v2 API.
            bridge.insert(QStringLiteral("supported"), model != QLatin1String("BSB001"));
            addBridge(bridge);
            emit probeFinished(host, true, QString());
        }
        if (!m_stopTimer.isActive())
            setRunning(m_probesRunning > 0);
    });
}

void HueDiscovery::addBridge(const QVariantMap &bridge)
{
    const QString id = bridge.value(QStringLiteral("id")).toString();
    for (int i = 0; i < m_bridges.size(); ++i) {
        if (m_bridges.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            m_bridges.removeAt(i);
            break;
        }
    }
    m_bridges.append(bridge);
    std::sort(m_bridges.begin(), m_bridges.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                    b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    emit bridgesChanged();
}

void HueDiscovery::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}
