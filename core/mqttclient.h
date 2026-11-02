// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

class QTcpSocket;

// The bit of MQTT 3.1.1 a zigbee2mqtt client needs: connect with a user name and password,
// subscribe, and publish and receive with QoS 0. Sailfish's Qt 5.6 has no QtMqtt.
namespace Mqtt {

enum PacketType {
    Connect = 1,
    ConnAck = 2,
    Publish = 3,
    Subscribe = 8,
    SubAck = 9,
    PingReq = 12,
    PingResp = 13,
    Disconnect = 14
};

struct Packet {
    int type = 0;
    int flags = 0;
    QByteArray body;
};

QByteArray connectPacket(const QString &clientId, const QString &user, const QString &password,
                         int keepAliveSecs);
QByteArray subscribePacket(quint16 packetId, const QStringList &topics);
QByteArray publishPacket(const QString &topic, const QByteArray &payload, bool retain = false);
QByteArray pingPacket();
QByteArray disconnectPacket();

// Of a PUBLISH packet (any QoS); false if it's malformed.
bool parsePublish(const Packet &packet, QString *topic, QByteArray *payload);

// Splits the bytes from the broker into packets.
class Parser
{
public:
    // Complete packets; the rest waits for more bytes. Sets failed() on garbage.
    QList<Packet> feed(const QByteArray &bytes);
    bool failed() const { return m_failed; }
    void reset();

private:
    QByteArray m_buffer;
    bool m_failed = false;
};

} // namespace Mqtt

struct MqttConfig {
    QString host;
    int port = 1883;
    QString user;
    QString password;

    bool isValid() const { return !host.isEmpty() && port > 0 && port < 65536; }
};

// Connection to a broker that keeps itself up: it reconnects with exponential backoff and
// subscribes again each time. A broker refusing the user name or password is final, until the
// config changes or start() is called again.
class MqttClient : public QObject
{
    Q_OBJECT
public:
    enum State {
        Idle,
        Connecting,
        Connected,
        WaitingToReconnect,
        Refused         // bad user name or password, or not authorised
    };
    Q_ENUM(State)

    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient() override;

    void setConfig(const MqttConfig &config);
    MqttConfig config() const { return m_config; }
    State state() const { return m_state; }

    void setReconnectDelays(int initialMsecs, int maxMsecs);
    void setKeepAlive(int secs);
    void setTimeout(int msecs);

    // Kept, and subscribed again after every reconnect.
    void subscribe(const QString &topicFilter);
    // Takes effect on the next connect.
    void clearSubscriptions() { m_subscriptions.clear(); }
    // QoS 0: false (and nothing sent) when not connected.
    bool publish(const QString &topic, const QByteArray &payload, bool retain = false);

public slots:
    void start();
    void stop();
    void reconnectNow();

signals:
    void stateChanged(MqttClient::State state);
    void connected();
    void messageReceived(const QString &topic, const QByteArray &payload);
    void errorOccurred(const QString &error);

private:
    void open();
    void onReadyRead();
    void handle(const Mqtt::Packet &packet);
    void fail(const QString &error);
    void scheduleReconnect();
    void teardown();
    void setState(State state);

    MqttConfig m_config;
    State m_state = Idle;
    QPointer<QTcpSocket> m_socket;
    Mqtt::Parser m_parser;
    QStringList m_subscriptions;
    quint16 m_nextPacketId = 1;
    bool m_awaitingPong = false;

    QTimer m_reconnectTimer;
    QTimer m_watchdog;  // connecting takes too long
    QTimer m_keepAlive; // ping, and notice when the pong doesn't come
    int m_initialDelay = 1000;
    int m_maxDelay = 60 * 1000;
    int m_reconnectDelay = 1000;
    int m_keepAliveSecs = 60;
};

#endif // MQTTCLIENT_H
