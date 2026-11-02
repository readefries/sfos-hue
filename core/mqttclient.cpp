// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mqttclient.h"

#include <QTcpSocket>
#include <QUuid>

namespace Mqtt {

namespace {

void append16(QByteArray *data, quint16 value)
{
    data->append(char(value >> 8));
    data->append(char(value & 0xff));
}

void appendString(QByteArray *data, const QByteArray &string)
{
    append16(data, quint16(string.size()));
    data->append(string);
}

QByteArray packet(int type, int flags, const QByteArray &body)
{
    QByteArray data;
    data.append(char((type << 4) | flags));
    // Remaining length: 7 bits per byte, least significant first.
    int length = body.size();
    do {
        char byte = char(length % 128);
        length /= 128;
        if (length > 0)
            byte = char(byte | 0x80);
        data.append(byte);
    } while (length > 0);
    return data + body;
}

quint16 read16(const QByteArray &data, int offset)
{
    return quint16((quint8(data.at(offset)) << 8) | quint8(data.at(offset + 1)));
}

} // namespace

QByteArray connectPacket(const QString &clientId, const QString &user, const QString &password,
                         int keepAliveSecs)
{
    int flags = 0x02; // clean session: nothing to resume, subscriptions are sent again anyway
    if (!user.isEmpty())
        flags |= 0x80;
    if (!user.isEmpty() && !password.isEmpty())
        flags |= 0x40;
    QByteArray body;
    appendString(&body, "MQTT");
    body.append(char(4)); // 3.1.1
    body.append(char(flags));
    append16(&body, quint16(keepAliveSecs));
    appendString(&body, clientId.toUtf8());
    if (flags & 0x80)
        appendString(&body, user.toUtf8());
    if (flags & 0x40)
        appendString(&body, password.toUtf8());
    return packet(Connect, 0, body);
}

QByteArray subscribePacket(quint16 packetId, const QStringList &topics)
{
    QByteArray body;
    append16(&body, packetId);
    for (const QString &topic : topics) {
        appendString(&body, topic.toUtf8());
        body.append(char(0)); // QoS 0
    }
    return packet(Subscribe, 0x02, body);
}

QByteArray publishPacket(const QString &topic, const QByteArray &payload, bool retain)
{
    QByteArray body;
    appendString(&body, topic.toUtf8());
    body.append(payload);
    return packet(Publish, retain ? 0x01 : 0, body);
}

QByteArray pingPacket()
{
    return packet(PingReq, 0, QByteArray());
}

QByteArray disconnectPacket()
{
    return packet(Disconnect, 0, QByteArray());
}

bool parsePublish(const Packet &packet, QString *topic, QByteArray *payload)
{
    if (packet.type != Publish || packet.body.size() < 2)
        return false;
    const int topicLength = read16(packet.body, 0);
    // QoS 1 and 2 have a packet id after the topic.
    const int offset = 2 + topicLength + ((packet.flags & 0x06) ? 2 : 0);
    if (offset > packet.body.size())
        return false;
    *topic = QString::fromUtf8(packet.body.mid(2, topicLength));
    *payload = packet.body.mid(offset);
    return true;
}

QList<Packet> Parser::feed(const QByteArray &bytes)
{
    QList<Packet> packets;
    if (m_failed)
        return packets;
    m_buffer.append(bytes);
    while (m_buffer.size() >= 2) {
        int length = 0;
        int multiplier = 1;
        int pos = 1;
        bool complete = false;
        while (pos < m_buffer.size() && pos <= 4) {
            const quint8 byte = quint8(m_buffer.at(pos++));
            length += (byte & 0x7f) * multiplier;
            multiplier *= 128;
            if (!(byte & 0x80)) {
                complete = true;
                break;
            }
        }
        if (!complete) {
            if (pos > 4) {
                m_failed = true; // a length takes four bytes at most
                m_buffer.clear();
            }
            break;
        }
        if (m_buffer.size() < pos + length)
            break;
        Packet packet;
        packet.type = quint8(m_buffer.at(0)) >> 4;
        packet.flags = m_buffer.at(0) & 0x0f;
        packet.body = m_buffer.mid(pos, length);
        m_buffer.remove(0, pos + length);
        packets.append(packet);
    }
    return packets;
}

void Parser::reset()
{
    m_buffer.clear();
    m_failed = false;
}

} // namespace Mqtt

MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &MqttClient::open);

    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(15 * 1000);
    connect(&m_watchdog, &QTimer::timeout, this, [this]() {
        fail(tr("The MQTT broker did not respond"));
    });

    connect(&m_keepAlive, &QTimer::timeout, this, [this]() {
        if (m_awaitingPong) {
            fail(tr("Lost the connection to the MQTT broker"));
            return;
        }
        m_awaitingPong = true;
        if (m_socket)
            m_socket->write(Mqtt::pingPacket());
    });
}

MqttClient::~MqttClient()
{
    teardown();
}

void MqttClient::setConfig(const MqttConfig &config)
{
    m_config = config;
}

void MqttClient::setReconnectDelays(int initialMsecs, int maxMsecs)
{
    m_initialDelay = initialMsecs;
    m_maxDelay = maxMsecs;
    m_reconnectDelay = initialMsecs;
}

void MqttClient::setKeepAlive(int secs)
{
    m_keepAliveSecs = secs;
}

void MqttClient::setTimeout(int msecs)
{
    m_watchdog.setInterval(msecs);
}

void MqttClient::subscribe(const QString &topicFilter)
{
    if (m_subscriptions.contains(topicFilter))
        return;
    m_subscriptions.append(topicFilter);
    if (m_state == Connected)
        m_socket->write(Mqtt::subscribePacket(m_nextPacketId++, QStringList(topicFilter)));
}

bool MqttClient::publish(const QString &topic, const QByteArray &payload, bool retain)
{
    if (m_state != Connected || !m_socket)
        return false;
    m_socket->write(Mqtt::publishPacket(topic, payload, retain));
    return true;
}

void MqttClient::start()
{
    m_reconnectDelay = m_initialDelay;
    open();
}

void MqttClient::stop()
{
    if (m_socket && m_state == Connected) {
        m_socket->write(Mqtt::disconnectPacket());
        m_socket->flush();
    }
    teardown();
    m_reconnectDelay = m_initialDelay;
    setState(Idle);
}

void MqttClient::reconnectNow()
{
    if (m_state == Refused || !m_config.isValid())
        return;
    start();
}

void MqttClient::open()
{
    if (!m_config.isValid())
        return;
    teardown();
    setState(Connecting);
    m_parser.reset();
    m_awaitingPong = false;
    m_socket = new QTcpSocket(this);
    QTcpSocket *socket = m_socket;
    connect(socket, &QTcpSocket::connected, this, [this, socket]() {
        // Unique, so a second phone (or a stale session) doesn't kick this one off.
        const QString clientId = QStringLiteral("lampje-")
                + QUuid::createUuid().toString().mid(1, 8);
        socket->write(Mqtt::connectPacket(clientId, m_config.user, m_config.password, m_keepAliveSecs));
    });
    connect(socket, &QTcpSocket::readyRead, this, &MqttClient::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        if (socket == m_socket)
            fail(tr("The MQTT broker closed the connection"));
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket, &QTcpSocket::errorOccurred,
#else
    connect(socket, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error),
#endif
            this, [this, socket](QAbstractSocket::SocketError error) {
        if (socket != m_socket || error == QAbstractSocket::RemoteHostClosedError)
            return; // disconnected() handles that
        fail(socket->errorString());
    });
    m_watchdog.start();
    socket->connectToHost(m_config.host, quint16(m_config.port));
}

void MqttClient::onReadyRead()
{
    if (!m_socket)
        return;
    const QList<Mqtt::Packet> packets = m_parser.feed(m_socket->readAll());
    if (m_parser.failed()) {
        fail(tr("The MQTT broker sent something unexpected"));
        return;
    }
    for (const Mqtt::Packet &packet : packets) {
        handle(packet);
        if (!m_socket)
            return; // handling it ended the connection
    }
}

void MqttClient::handle(const Mqtt::Packet &packet)
{
    m_awaitingPong = false; // anything at all shows the connection is alive
    switch (packet.type) {
    case Mqtt::ConnAck: {
        const int code = packet.body.size() >= 2 ? quint8(packet.body.at(1)) : -1;
        if (code == 4 || code == 5) {
            teardown();
            setState(Refused);
            emit errorOccurred(code == 4 ? tr("The MQTT broker did not accept the user name or password")
                                         : tr("The MQTT broker did not allow this user to connect"));
            return;
        }
        if (code != 0) {
            fail(tr("The MQTT broker refused the connection (%1)").arg(code));
            return;
        }
        m_watchdog.stop();
        m_reconnectDelay = m_initialDelay;
        if (!m_subscriptions.isEmpty())
            m_socket->write(Mqtt::subscribePacket(m_nextPacketId++, m_subscriptions));
        if (m_keepAliveSecs > 0)
            m_keepAlive.start(m_keepAliveSecs * 1000);
        setState(Connected);
        emit connected();
        break;
    }
    case Mqtt::Publish: {
        QString topic;
        QByteArray payload;
        if (m_state == Connected && Mqtt::parsePublish(packet, &topic, &payload))
            emit messageReceived(topic, payload);
        break;
    }
    case Mqtt::SubAck:
        for (int i = 2; i < packet.body.size(); ++i) {
            if (quint8(packet.body.at(i)) == 0x80) {
                emit errorOccurred(tr("The MQTT broker did not allow this user to subscribe"));
                break;
            }
        }
        break;
    default:
        break;
    }
}

void MqttClient::fail(const QString &error)
{
    teardown();
    emit errorOccurred(error);
    scheduleReconnect();
}

void MqttClient::scheduleReconnect()
{
    setState(WaitingToReconnect);
    m_reconnectTimer.start(m_reconnectDelay);
    m_reconnectDelay = qMin(m_reconnectDelay * 2, m_maxDelay);
}

void MqttClient::teardown()
{
    m_watchdog.stop();
    m_keepAlive.stop();
    m_reconnectTimer.stop();
    // Clear the pointer first: abort() emits signals whose handlers must ignore it.
    QTcpSocket *socket = m_socket;
    m_socket = nullptr;
    if (socket) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

void MqttClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}
