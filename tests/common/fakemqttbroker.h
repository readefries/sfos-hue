// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef FAKEMQTTBROKER_H
#define FAKEMQTTBROKER_H

#include "mqttclient.h"

#include <QFile>
#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>

// Stand-in for an MQTT broker: accepts (or refuses) connections, records what clients
// subscribe to and publish, keeps retained messages and delivers to matching subscriptions.
class FakeMqttBroker : public QTcpServer
{
    Q_OBJECT
public:
    struct Message {
        QString topic;
        QByteArray payload;
    };

    QString user, password;     // what was connected with, last
    int connAckCode = 0;
    bool answerPings = true;
    int connects = 0;
    int pings = 0;
    QStringList subscriptions;
    QList<Message> published;   // by clients
    QHash<QString, QByteArray> retained;

    FakeMqttBroker() { listen(QHostAddress::LocalHost); }

    MqttConfig config(const QString &user = QString(), const QString &password = QString()) const
    {
        MqttConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.port = serverPort();
        config.user = user;
        config.password = password;
        return config;
    }

    static QByteArray fixture(const char *name)
    {
        QFile file(QStringLiteral(FIXTURE_DIR "/") + QString::fromLatin1(name));
        file.open(QIODevice::ReadOnly);
        return file.readAll();
    }

    static bool matches(const QString &filter, const QString &topic)
    {
        const QStringList f = filter.split(QLatin1Char('/'));
        const QStringList t = topic.split(QLatin1Char('/'));
        for (int i = 0; i < f.size(); ++i) {
            if (f.at(i) == QLatin1String("#"))
                return true;
            if (i >= t.size() || (f.at(i) != QLatin1String("+") && f.at(i) != t.at(i)))
                return false;
        }
        return f.size() == t.size();
    }

    // As a device would: to every client with a matching subscription.
    void publish(const QString &topic, const QByteArray &payload, bool retain = false)
    {
        if (retain)
            retained.insert(topic, payload);
        for (auto it = m_subscribed.begin(); it != m_subscribed.end(); ++it) {
            for (const QString &filter : it.value()) {
                if (matches(filter, topic)) {
                    it.key()->write(Mqtt::publishPacket(topic, payload, false));
                    break;
                }
            }
        }
    }

    void dropClients()
    {
        for (QTcpSocket *socket : findChildren<QTcpSocket *>())
            socket->abort();
        m_subscribed.clear();
    }

    QList<Message> publishedTo(const QString &topic) const
    {
        QList<Message> list;
        for (const Message &message : published) {
            if (message.topic == topic)
                list.append(message);
        }
        return list;
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        QTcpSocket *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        Mqtt::Parser *parser = new Mqtt::Parser;
        connect(socket, &QObject::destroyed, this, [this, socket, parser]() {
            m_subscribed.remove(socket);
            delete parser;
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, parser]() {
            for (const Mqtt::Packet &packet : parser->feed(socket->readAll()))
                handle(socket, packet);
        });
    }

private:
    static QByteArray readString(const QByteArray &data, int *pos)
    {
        const int length = (quint8(data.at(*pos)) << 8) | quint8(data.at(*pos + 1));
        const QByteArray string = data.mid(*pos + 2, length);
        *pos += 2 + length;
        return string;
    }

    void handle(QTcpSocket *socket, const Mqtt::Packet &packet)
    {
        switch (packet.type) {
        case Mqtt::Connect: {
            ++connects;
            int pos = 0;
            readString(packet.body, &pos); // "MQTT"
            const int flags = quint8(packet.body.at(pos + 1));
            pos += 4;
            readString(packet.body, &pos); // client id
            user = (flags & 0x80) ? QString::fromUtf8(readString(packet.body, &pos)) : QString();
            password = (flags & 0x40) ? QString::fromUtf8(readString(packet.body, &pos)) : QString();
            socket->write(QByteArray::fromHex("2002") + char(0) + char(connAckCode));
            if (connAckCode != 0)
                socket->disconnectFromHost();
            break;
        }
        case Mqtt::Subscribe: {
            int pos = 2;
            QStringList filters;
            while (pos < packet.body.size()) {
                filters.append(QString::fromUtf8(readString(packet.body, &pos)));
                ++pos; // QoS
            }
            subscriptions += filters;
            m_subscribed[socket] += filters;
            QByteArray ack = packet.body.left(2);
            ack.append(QByteArray(filters.size(), '\0'));
            socket->write(QByteArray(1, char(0x90)) + char(ack.size()) + ack);
            for (auto it = retained.constBegin(); it != retained.constEnd(); ++it) {
                for (const QString &filter : filters) {
                    if (matches(filter, it.key())) {
                        socket->write(Mqtt::publishPacket(it.key(), it.value(), true));
                        break;
                    }
                }
            }
            break;
        }
        case Mqtt::Publish: {
            Message message;
            if (Mqtt::parsePublish(packet, &message.topic, &message.payload)) {
                published.append(message);
                emit messagePublished(message.topic, message.payload);
            }
            break;
        }
        case Mqtt::PingReq:
            ++pings;
            if (answerPings)
                socket->write(QByteArray::fromHex("d000"));
            break;
        default:
            break;
        }
    }

    QHash<QTcpSocket *, QStringList> m_subscribed;

signals:
    void messagePublished(const QString &topic, const QByteArray &payload);
};

#endif // FAKEMQTTBROKER_H
