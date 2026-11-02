// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fakehuebridge.h"
#include "fakemqttbroker.h"

#include "huebridge.h"
#include "hueclient.h"
#include "huecolor.h"
#include "huecommandqueue.h"
#include "huediscovery.h"
#include "huejson.h"
#include "huemdns.h"
#include "huemodels.h"
#include "huepairing.h"
#include "hueresourcestore.h"
#include "huessestream.h"
#include "mqttclient.h"
#include "zigbee2mqtt.h"

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtMath>
#include <QtTest>

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QJsonArray updateEvent(const QJsonObject &resource)
{
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("update"));
    event.insert(QStringLiteral("data"), QJsonArray() << resource);
    return QJsonArray() << event;
}

QJsonValue at(const QJsonObject &obj, const char *a, const char *b)
{
    return HueJson::at(obj, {QString::fromLatin1(a), QString::fromLatin1(b)});
}

QJsonArray fixtureData()
{
    return FakeHueBridge::fixture().value(QStringLiteral("data")).toArray();
}

void write16(QByteArray *data, quint16 value)
{
    data->append(char(value >> 8));
    data->append(char(value & 0xff));
}

void write32(QByteArray *data, quint32 value)
{
    write16(data, quint16(value >> 16));
    write16(data, quint16(value & 0xffff));
}

void writeName(QByteArray *data, const QList<QByteArray> &labels, int pointer = -1)
{
    for (const QByteArray &label : labels) {
        data->append(char(label.size()));
        data->append(label);
    }
    if (pointer >= 0)
        write16(data, quint16(0xc000 | pointer));
    else
        data->append('\0');
}

} // namespace

class TestLampje : public QObject
{
    Q_OBJECT

private slots:
    void jsonMerge()
    {
        const QJsonObject base = json(R"({"on": {"on": false}, "dimming": {"brightness": 10, "min": 1}, "list": [1, 2]})");
        const QJsonObject patch = json(R"({"dimming": {"brightness": 50}, "list": [3]})");
        const QJsonObject merged = HueJson::merged(base, patch);
        QCOMPARE(at(merged, "dimming", "brightness").toDouble(), 50.0);
        QCOMPARE(at(merged, "dimming", "min").toDouble(), 1.0);
        QCOMPARE(at(merged, "on", "on").toBool(), false);
        QCOMPARE(merged.value(QStringLiteral("list")).toArray().size(), 1);
    }

    void sseParser()
    {
        HueSseParser parser;
        QList<HueSseParser::Event> events;
        events += parser.feed(": hi\n\nid: 1\nda");
        QCOMPARE(events.size(), 0);
        events += parser.feed("ta: [1]\n");
        events += parser.feed("\ndata: a\r\ndata: b\r\n");
        QCOMPARE(events.size(), 1);
        events += parser.feed("\r\n");
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.at(0).id, QByteArray("1"));
        QCOMPARE(events.at(0).data, QByteArray("[1]"));
        QCOMPARE(events.at(1).data, QByteArray("a\nb"));
        QVERIFY(events.at(1).id.isEmpty());
    }

    void mqttPackets()
    {
        const QByteArray publish = Mqtt::publishPacket(QStringLiteral("a/b"), QByteArray(300, 'x'));
        QCOMPARE(quint8(publish.at(0)), quint8(0x30));
        // 2 + 3 + 300 = 305 bytes: two length bytes.
        QCOMPARE(quint8(publish.at(1)), quint8(305 % 128 | 0x80));
        QCOMPARE(quint8(publish.at(2)), quint8(305 / 128));

        Mqtt::Parser parser;
        QList<Mqtt::Packet> packets = parser.feed(publish.left(2));
        packets += parser.feed(publish.mid(2, 100));
        QCOMPARE(packets.size(), 0);
        packets += parser.feed(publish.mid(102) + Mqtt::pingPacket());
        QCOMPARE(packets.size(), 2);
        QString topic;
        QByteArray payload;
        QVERIFY(Mqtt::parsePublish(packets.at(0), &topic, &payload));
        QCOMPARE(topic, QStringLiteral("a/b"));
        QCOMPARE(payload, QByteArray(300, 'x'));
        QCOMPARE(packets.at(1).type, int(Mqtt::PingReq));

        // QoS 1 carries a packet id after the topic.
        Mqtt::Packet qos1;
        qos1.type = Mqtt::Publish;
        qos1.flags = 0x02;
        qos1.body = QByteArray::fromHex("0001") + "t" + QByteArray::fromHex("0007") + "data";
        QVERIFY(Mqtt::parsePublish(qos1, &topic, &payload));
        QCOMPARE(payload, QByteArray("data"));

        const QByteArray connect = Mqtt::connectPacket(QStringLiteral("id"), QStringLiteral("u"), QStringLiteral("p"), 60);
        QCOMPARE(connect.toHex(), QByteArray("101400044d51545404c2003c00026964000175000170"));

        parser.reset();
        parser.feed(QByteArray::fromHex("30ffffffff01"));
        QVERIFY(parser.failed());
    }

    void mqttClient()
    {
        FakeMqttBroker broker;
        broker.retained.insert(QStringLiteral("z/retained"), "kept");
        MqttClient client;
        client.setConfig(broker.config(QStringLiteral("lampje"), QStringLiteral("secret")));
        client.setReconnectDelays(10, 50);
        QSignalSpy messages(&client, &MqttClient::messageReceived);
        client.subscribe(QStringLiteral("z/#"));
        QVERIFY(!client.publish(QStringLiteral("z/x"), "early"));
        client.start();
        QTRY_COMPARE(client.state(), MqttClient::Connected);
        QCOMPARE(broker.user, QStringLiteral("lampje"));
        QCOMPARE(broker.password, QStringLiteral("secret"));
        QTRY_COMPARE(messages.size(), 1);
        QCOMPARE(messages.at(0).at(1).toByteArray(), QByteArray("kept"));

        QVERIFY(client.publish(QStringLiteral("z/x"), "hello"));
        QTRY_COMPARE(broker.publishedTo(QStringLiteral("z/x")).size(), 1);
        broker.publish(QStringLiteral("z/device"), "{}");
        broker.publish(QStringLiteral("other"), "{}");
        QTRY_COMPARE(messages.size(), 2); // z/device, not other
        QCOMPARE(messages.at(1).at(0).toString(), QStringLiteral("z/device"));

        // Lost: reconnects and subscribes again.
        broker.dropClients();
        QTRY_COMPARE(broker.connects, 2);
        QTRY_COMPARE(client.state(), MqttClient::Connected);
        QTRY_COMPARE(broker.subscriptions, QStringList() << QStringLiteral("z/#") << QStringLiteral("z/#"));

        client.stop();
        QCOMPARE(client.state(), MqttClient::Idle);
    }

    void mqttClientRefused()
    {
        FakeMqttBroker broker;
        broker.connAckCode = 4;
        MqttClient client;
        client.setConfig(broker.config(QStringLiteral("lampje"), QStringLiteral("wrong")));
        client.setReconnectDelays(10, 50);
        QSignalSpy errors(&client, &MqttClient::errorOccurred);
        client.start();
        QTRY_COMPARE(client.state(), MqttClient::Refused);
        QVERIFY(errors.size() >= 1);
        QTest::qWait(100);
        QCOMPARE(broker.connects, 1); // no retrying with a bad password
        QCOMPARE(client.state(), MqttClient::Refused);
    }

    void mqttKeepAlive()
    {
        FakeMqttBroker broker;
        broker.answerPings = false;
        MqttClient client;
        client.setConfig(broker.config());
        client.setReconnectDelays(10, 50);
        client.setKeepAlive(1);
        client.start();
        QTRY_COMPARE(client.state(), MqttClient::Connected);
        // A ping after a second, no pong a second later: gone, so connect again.
        QTRY_COMPARE_WITH_TIMEOUT(broker.connects, 2, 4000);
        QVERIFY(broker.pings >= 1);
    }

    void zigbeeDevices()
    {
        FakeMqttBroker broker;
        broker.retained.insert(QStringLiteral("zigbee2mqtt/bridge/devices"),
                               FakeMqttBroker::fixture("z2m-devices.json"));
        HueResourceStore store;
        MqttClient mqtt;
        mqtt.setConfig(broker.config());
        mqtt.setReconnectDelays(10, 50);
        Zigbee2Mqtt zigbee(&mqtt, &store);
        mqtt.start();

        const QString strip = QStringLiteral("z2m:0x0000000000000001:light");
        const QString hallLeft = QStringLiteral("z2m:0x0000000000000002:light:left");
        const QString hallRight = QStringLiteral("z2m:0x0000000000000002:light:right");
        const QString office = QStringLiteral("z2m:0x0000000000000003:light");
        QTRY_VERIFY(store.contains(strip));
        QCOMPARE(store.idsOfType(QStringLiteral("light")).size(), 5); // strip, hall twice, office, bedroom
        QCOMPARE(store.name(hallLeft), QStringLiteral("hall left"));
        QCOMPARE(store.name(office), QStringLiteral("office"));
        QVERIFY(store.resource(strip).value(QStringLiteral("dimming")).isObject());
        QVERIFY(!store.resource(office).value(QStringLiteral("dimming")).isObject());
        QVERIFY(!store.resource(strip).value(QStringLiteral("color")).isObject());
        QCOMPARE(store.deviceOf(hallRight), QStringLiteral("z2m:0x0000000000000002"));
        QVERIFY(store.isReachable(hallLeft));

        // States asked for, as they may not be retained; not of the sleeping motion sensor.
        QTRY_COMPARE(broker.publishedTo(QStringLiteral("zigbee2mqtt/hall/get")).size(), 1);
        QCOMPARE(QJsonDocument::fromJson(broker.publishedTo(QStringLiteral("zigbee2mqtt/hall/get")).first().payload).object(),
                 json(R"({"state_left": "", "state_right": ""})"));
        QCOMPARE(QJsonDocument::fromJson(broker.publishedTo(QStringLiteral("zigbee2mqtt/strip/get")).first().payload).object(),
                 json(R"({"state": "", "brightness": ""})"));
        QVERIFY(broker.publishedTo(QStringLiteral("zigbee2mqtt/hall_motion/get")).isEmpty());

        broker.publish(QStringLiteral("zigbee2mqtt/hall"), R"({"state_left": "ON", "state_right": "OFF", "power": 12})");
        broker.publish(QStringLiteral("zigbee2mqtt/strip"), R"({"state": "ON", "brightness": 127})");
        QTRY_VERIFY(at(store.resource(strip), "on", "on").toBool());
        QVERIFY(at(store.resource(hallLeft), "on", "on").toBool());
        QVERIFY(!at(store.resource(hallRight), "on", "on").toBool());
        QCOMPARE(at(store.resource(strip), "dimming", "brightness").toDouble(), 50.0);


        // Sensors look like the bridge's.
        HueSensorModel sensors;
        sensors.setStore(&store);
        QCOMPARE(sensors.rowCount(), 1);
        broker.publish(QStringLiteral("zigbee2mqtt/hall_motion"),
                       R"({"occupancy": true, "illuminance": 100, "temperature": 21.5, "battery": 12})");
        QTRY_COMPARE(sensors.data(sensors.index(0), HueSensorModel::MotionRole).toBool(), true);
        QCOMPARE(sensors.data(sensors.index(0), HueSensorModel::KindRole).toString(), QStringLiteral("motion"));
        QVERIFY(qAbs(sensors.data(sensors.index(0), HueSensorModel::LightLevelRole).toDouble() - 100) < 0.1);
        QCOMPARE(sensors.data(sensors.index(0), HueSensorModel::TemperatureRole).toDouble(), 21.5);
        QCOMPARE(sensors.data(sensors.index(0), HueSensorModel::BatteryLevelRole).toInt(), 12);
        QCOMPARE(sensors.data(sensors.index(0), HueSensorModel::BatteryLowRole).toBool(), true);
        QVERIFY(sensors.data(sensors.index(0), HueSensorModel::MotionChangedRole).toDateTime().isValid());

        // Availability, and the broker's connection.
        broker.publish(QStringLiteral("zigbee2mqtt/office/availability"), R"({"state": "offline"})");
        QTRY_VERIFY(!store.isReachable(office));
        QVERIFY(store.isReachable(hallLeft));
        broker.dropClients();
        QTRY_VERIFY(!store.isReachable(hallLeft));
        QTRY_VERIFY(store.isReachable(hallLeft)); // back, after reconnecting

        // The bridge loading its resources leaves Zigbee's alone, and the other way round.
        store.reset(fixtureData());
        QVERIFY(store.contains(strip));
        QVERIFY(store.contains(QStringLiteral("light-1")));
        zigbee.clear();
        QVERIFY(!store.contains(strip));
        QVERIFY(store.contains(QStringLiteral("light-1")));
    }

    void zigbeeColorLight()
    {
        // Not one of the fixture's; how a colour bulb exposes itself.
        HueResourceStore store;
        MqttClient mqtt;
        Zigbee2Mqtt zigbee(&mqtt, &store);
        zigbee.setBaseTopic(QStringLiteral("z2m/"));
        zigbee.handleMessage(QStringLiteral("z2m/bridge/devices"), R"([{
            "ieee_address": "0xaa", "friendly_name": "bulb", "type": "Router",
            "definition": {"model": "X", "vendor": "Y", "description": "Colour bulb", "exposes": [
                {"type": "light", "features": [
                    {"type": "binary", "name": "state", "property": "state"},
                    {"type": "numeric", "name": "brightness", "property": "brightness", "value_max": 254},
                    {"type": "numeric", "name": "color_temp", "property": "color_temp", "value_min": 150, "value_max": 500},
                    {"type": "composite", "name": "color_xy", "property": "color"}]}]}}])");
        const QString bulb = QStringLiteral("z2m:0xaa:light");
        QVERIFY(store.contains(bulb));
        zigbee.handleMessage(QStringLiteral("z2m/bulb"),
                             R"({"state": "ON", "color_mode": "xy", "color_temp": 300, "color": {"x": 0.6, "y": 0.3}})");
        const QJsonObject light = store.resource(bulb);
        QCOMPARE(HueJson::at(light, {QStringLiteral("color"), QStringLiteral("xy"), QStringLiteral("x")}).toDouble(), 0.6);
        QCOMPARE(at(light, "color_temperature", "mirek").toInt(), 300);
        QVERIFY(!at(light, "color_temperature", "mirek_valid").toBool());
        QCOMPARE(HueJson::at(light, {QStringLiteral("color_temperature"), QStringLiteral("mirek_schema"),
                                     QStringLiteral("mirek_minimum")}).toInt(), 150);
        zigbee.handleMessage(QStringLiteral("z2m/bulb"), R"({"color_mode": "color_temp"})");
        QVERIFY(at(store.resource(bulb), "color_temperature", "mirek_valid").toBool());
        // Not connected to a broker: not reachable.
        QVERIFY(!store.isReachable(bulb));
    }

    void zigbeeControl()
    {
        FakeMqttBroker broker;
        broker.retained.insert(QStringLiteral("zigbee2mqtt/bridge/devices"),
                               FakeMqttBroker::fixture("z2m-devices.json"));
        QTemporaryDir dir;
        HueClient client(dir.path() + QStringLiteral("/lampje.conf"));
        client.zigbee()->setThrottle(200);
        client.setMqttConfig(QStringLiteral("127.0.0.1"), broker.serverPort(), QStringLiteral("lampje"),
                             QStringLiteral("secret"), QStringLiteral("zigbee2mqtt/"));
        QCOMPARE(client.mqttBaseTopic(), QStringLiteral("zigbee2mqtt/"));
        QTRY_COMPARE(client.mqttState(), HueClient::MqttConnected);
        HueResourceStore *store = client.store();
        const QString strip = QStringLiteral("z2m:0x0000000000000001:light");
        const QString hallRight = QStringLiteral("z2m:0x0000000000000002:light:right");
        const QString office = QStringLiteral("z2m:0x0000000000000003:light");
        QTRY_VERIFY(store->contains(strip));
        auto sets = [&broker](const char *device) {
            QList<QJsonObject> list;
            for (const FakeMqttBroker::Message &message
                 : broker.publishedTo(QStringLiteral("zigbee2mqtt/%1/set").arg(QString::fromLatin1(device))))
                list.append(QJsonDocument::fromJson(message.payload).object());
            return list;
        };

        client.setOn(hallRight, true);
        QVERIFY(at(store->resource(hallRight), "on", "on").toBool()); // right away
        QTRY_COMPARE(sets("hall").size(), 1);
        QCOMPARE(sets("hall").first(), json(R"({"state_right": "ON"})"));

        // A slider being dragged: the first goes out, the rest merge into one.
        QTest::qWait(250); // past the hall's round
        client.setBrightness(strip, 20);
        client.setBrightness(strip, 30);
        client.setBrightness(strip, 50);
        QCOMPARE(at(store->resource(strip), "dimming", "brightness").toDouble(), 50.0);
        QTRY_COMPARE(sets("strip").size(), 2);
        QCOMPARE(sets("strip").at(0), json(R"({"state": "ON", "brightness": 51})"));
        QCOMPARE(sets("strip").at(1), json(R"({"state": "ON", "brightness": 127})"));

        // An on/off switch takes no brightness, nor does it start to look dimmable.
        client.setBrightness(office, 40);
        QTRY_COMPARE(sets("office").size(), 1);
        QCOMPARE(sets("office").first(), json(R"({"state": "ON"})"));
        QVERIFY(!store->resource(office).value(QStringLiteral("dimming")).isObject());

        client.allOff();
        QTRY_COMPARE(sets("office").size(), 2);
        QCOMPARE(sets("office").last(), json(R"({"state": "OFF"})"));
        QTRY_COMPARE(sets("hall").size(), 2);
        QCOMPARE(sets("hall").last(), json(R"({"state_right": "OFF"})"));

        // Renaming isn't done here; nothing goes out.
        client.renameLight(office, QStringLiteral("Desk"));
        QTest::qWait(300);
        QCOMPARE(sets("office").size(), 2);
        QCOMPARE(store->name(office), QStringLiteral("office"));

        client.forgetMqtt();
        QCOMPARE(client.mqttState(), HueClient::MqttOff);
        QVERIFY(!store->contains(strip));
    }

    void mixedRooms()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"success": {"username": "client-key"}}])");
        FakeMqttBroker broker;
        broker.retained.insert(QStringLiteral("zigbee2mqtt/bridge/devices"),
                               FakeMqttBroker::fixture("z2m-devices.json"));
        QTemporaryDir dir;
        const QString settings = dir.path() + QStringLiteral("/lampje.conf");
        const QString hallLeft = QStringLiteral("z2m:0x0000000000000002:light:left");
        const QString hallRight = QStringLiteral("z2m:0x0000000000000002:light:right");
        const QString office = QStringLiteral("z2m:0x0000000000000003:light");
        const QString strip = QStringLiteral("z2m:0x0000000000000001:light");
        QString officeRoom;
        auto sets = [&broker](const char *device) {
            QList<QJsonObject> list;
            for (const FakeMqttBroker::Message &message
                 : broker.publishedTo(QStringLiteral("zigbee2mqtt/%1/set").arg(QString::fromLatin1(device))))
                list.append(QJsonDocument::fromJson(message.payload).object());
            return list;
        };
        {
            HueClient client(settings);
            client.zigbee()->setThrottle(0);
            client.pair(fake.host(), QStringLiteral("001788fffe00cafe"), QStringLiteral("Fake bridge"));
            client.setMqttConfig(QStringLiteral("127.0.0.1"), broker.serverPort(), QString(), QString(), QString());
            QTRY_COMPARE(client.state(), HueClient::Connected);
            HueResourceStore *store = client.store();
            QTRY_VERIFY(store->contains(office));

            HueLightModel unplaced;
            unplaced.setStore(store);
            unplaced.setWithoutRoom(true);
            QCOMPARE(unplaced.rowCount(), 5); // all the Zigbee lights; the bridge's are in rooms

            // A Zigbee light in a room of the bridge's.
            client.setLightRoom(hallLeft, QStringLiteral("room-bed"));
            QCOMPARE(client.roomOfLight(hallLeft), QStringLiteral("room-bed"));
            {
                // Only switches: no slider.
                const QString switches = client.createRoom(QStringLiteral("Switches"), QString());
                client.setLightRoom(hallRight, switches);
                HueRoomModel room;
                room.setStore(store);
                room.setGroupId(switches);
                QCOMPARE(room.data(room.index(0), HueRoomModel::DimmableRole).toBool(), false);
                client.deleteGroup(switches);
            }
            QVERIFY(store->lightsOf(QStringLiteral("room-bed")).contains(hallLeft));
            QCOMPARE(unplaced.rowCount(), 4);
            // The bridge's own lights stay where the bridge has them.
            client.setLightRoom(QStringLiteral("light-1"), QStringLiteral("room-bed"));
            QCOMPARE(client.roomOfLight(QStringLiteral("light-1")), QStringLiteral("room-living"));

            // And one in a room of Lampje's own.
            officeRoom = client.createRoom(QStringLiteral(" Office "), QStringLiteral("office"));
            QVERIFY(officeRoom.startsWith(QLatin1String("local:")));
            QCOMPARE(store->name(officeRoom), QStringLiteral("Office"));
            client.setLightRoom(office, officeRoom);
            client.setLightRoom(strip, officeRoom);
            QCOMPARE(store->lightsOf(officeRoom).size(), 2);

            HueRoomModel rooms;
            rooms.setStore(store);
            auto row = [&rooms](const QString &id) {
                for (int i = 0; i < rooms.rowCount(); ++i) {
                    if (rooms.idAt(i) == id)
                        return i;
                }
                return -1;
            };
            QVERIFY(row(officeRoom) >= 0);
            QCOMPARE(rooms.data(rooms.index(row(officeRoom)), HueRoomModel::OnRole).toBool(), false);

            // Switching the room: its lights one by one; it's on when one is.
            QCOMPARE(rooms.data(rooms.index(row(officeRoom)), HueRoomModel::DimmableRole).toBool(), true);
            client.setBrightness(officeRoom, 50);
            QTRY_COMPARE(sets("strip").size(), 1);
            QCOMPARE(sets("strip").first(), json(R"({"state": "ON", "brightness": 127})"));
            QTest::qWait(50);
            QVERIFY(sets("office").isEmpty()); // only switches; dimming leaves it alone
            client.setOn(officeRoom, true);
            QTRY_COMPARE(sets("office").size(), 1);
            QCOMPARE(sets("office").first(), json(R"({"state": "ON"})"));
            QCOMPARE(rooms.data(rooms.index(row(officeRoom)), HueRoomModel::OnRole).toBool(), true);
            QCOMPARE(rooms.data(rooms.index(row(officeRoom)), HueRoomModel::BrightnessRole).toDouble(), 75.0);
            client.toggleGroup(officeRoom);
            QTRY_COMPARE(sets("office").size(), 2);
            QCOMPARE(sets("office").last(), json(R"({"state": "OFF"})"));

            // A mixed room: the bridge's lights through the grouped_light, the Zigbee one directly.
            const QJsonObject off = json(R"({"id": "gl-bed", "type": "grouped_light", "on": {"on": false}})");
            fake.push(updateEvent(off));
            QTRY_COMPARE(at(store->resource(QStringLiteral("gl-bed")), "on", "on").toBool(), false);
            QCOMPARE(rooms.data(rooms.index(row(QStringLiteral("room-bed"))), HueRoomModel::OnRole).toBool(), false);
            client.toggleGroup(QStringLiteral("room-bed"));
            QTRY_COMPARE(fake.puts().size(), 1);
            QCOMPARE(fake.puts().first().path, QByteArray("/clip/v2/resource/grouped_light/gl-bed"));
            QCOMPARE(fake.puts().first().body, QByteArray(R"({"on":{"on":true}})"));
            QTRY_COMPARE(sets("hall").size(), 1);
            QCOMPARE(sets("hall").first(), json(R"({"state_left": "ON"})"));
            QCOMPARE(rooms.data(rooms.index(row(QStringLiteral("room-bed"))), HueRoomModel::OnRole).toBool(), true);
            // The grouped_light says off, but the Zigbee light is on, so the room is.
            fake.push(updateEvent(off));
            QTRY_COMPARE(at(store->resource(QStringLiteral("gl-bed")), "on", "on").toBool(), false);
            QCOMPARE(rooms.data(rooms.index(row(QStringLiteral("room-bed"))), HueRoomModel::OnRole).toBool(), true);

            // Scenes are the bridge's: only its lights go in.
            client.createScene(QStringLiteral("room-bed"), QStringLiteral("Evening"));
            QTRY_COMPARE(fake.withMethod("POST").size(), 2);
            const QJsonArray actions = QJsonDocument::fromJson(fake.withMethod("POST").last().body).object()
                    .value(QStringLiteral("actions")).toArray();
            for (const QJsonValue &action : actions)
                QVERIFY(!HueJson::at(action.toObject(), {QStringLiteral("target"), QStringLiteral("rid")}).toString().startsWith(QLatin1String("z2m:")));

            // A zone with both: the bridge gets its lights, the phone keeps the Zigbee one.
            client.updateZone(QStringLiteral("zone-reading"), QStringLiteral("Reading"), QString(),
                              QStringList() << QStringLiteral("light-1") << office);
            QCOMPARE(client.lightsOf(QStringLiteral("zone-reading")).size(), 2);
            QVERIFY(client.lightsOf(QStringLiteral("zone-reading")).contains(office));
            QTRY_COMPARE(fake.puts().size(), 2);
            QCOMPARE(QJsonDocument::fromJson(fake.puts().last().body).object().value(QStringLiteral("children")).toArray().size(), 1);
            // Moving the light to another room keeps it in the zone.
            client.setLightRoom(office, QStringLiteral("room-living"));
            QVERIFY(client.lightsOf(QStringLiteral("zone-reading")).contains(office));
            QVERIFY(!store->lightsOf(officeRoom).contains(office));

            // A zone of only Zigbee lights is Lampje's own.
            client.createZone(QStringLiteral("Switches"), QString(), QStringList() << hallLeft << office);
            QTest::qWait(50);
            QCOMPARE(fake.withMethod("POST").size(), 2);
            QStringList zones = store->idsOfType(QStringLiteral("zone"));
            zones.removeAll(QStringLiteral("zone-reading"));
            QCOMPARE(zones.size(), 1);
            QVERIFY(zones.first().startsWith(QLatin1String("local:")));
            QCOMPARE(store->lightsOf(zones.first()).size(), 2);
            client.deleteZone(zones.first());
            QVERIFY(!store->contains(zones.first()));
        }
        {
            // Remembered.
            HueClient client(settings);
            HueResourceStore *store = client.store();
            QCOMPARE(store->name(officeRoom), QStringLiteral("Office"));
            QTRY_VERIFY(store->contains(strip));
            QVERIFY(store->lightsOf(officeRoom).contains(strip));
            QVERIFY(store->lightsOf(QStringLiteral("room-bed")).contains(hallLeft));
            QTRY_COMPARE(client.state(), HueClient::Connected); // the bridge's reset leaves them
            QVERIFY(store->lightsOf(QStringLiteral("room-bed")).contains(hallLeft));

            client.updateRoom(officeRoom, QStringLiteral("Work"), QString());
            QCOMPARE(store->name(officeRoom), QStringLiteral("Work"));
            client.deleteGroup(officeRoom);
            QVERIFY(!store->contains(officeRoom));
            QCOMPARE(client.roomOfLight(strip), QString());
            client.deleteGroup(QStringLiteral("room-bed")); // not Lampje's: stays
            QVERIFY(store->contains(QStringLiteral("room-bed")));
        }
    }

    void storeRelations()
    {
        HueResourceStore store;
        store.reset(fixtureData());
        QCOMPARE(store.type(QStringLiteral("room-living")), QStringLiteral("room"));
        QCOMPARE(store.groupedLightOf(QStringLiteral("room-living")), QStringLiteral("gl-living"));
        QCOMPARE(store.groupOfGroupedLight(QStringLiteral("gl-bed")), QStringLiteral("room-bed"));
        QStringList living = store.lightsOf(QStringLiteral("room-living"));
        living.sort();
        QCOMPARE(living, QStringList() << QStringLiteral("light-1") << QStringLiteral("light-2"));
        QCOMPARE(store.lightsOf(QStringLiteral("zone-reading")), QStringList(QStringLiteral("light-1")));
        QCOMPARE(store.connectivityOfLight(QStringLiteral("light-3")), QStringLiteral("zc-3"));
        QVERIFY(store.isReachable(QStringLiteral("light-1")));
        QVERIFY(!store.isReachable(QStringLiteral("light-3")));
        QCOMPARE(store.scenesOf(QStringLiteral("room-living")).size(), 2);
    }

    void storeEvents()
    {
        HueResourceStore store;
        store.reset(fixtureData());
        QSignalSpy changed(&store, &HueResourceStore::resourceChanged);
        QSignalSpy added(&store, &HueResourceStore::resourceAdded);
        QSignalSpy removed(&store, &HueResourceStore::resourceRemoved);

        const QJsonArray events = QJsonDocument::fromJson(R"([
            {"type": "update", "data": [{"id": "light-2", "type": "light", "on": {"on": true}},
                                        {"id": "unknown", "type": "light", "on": {"on": true}}]},
            {"type": "add", "data": [{"id": "scene-new", "type": "scene", "metadata": {"name": "New"},
                                      "group": {"rid": "room-bed", "rtype": "room"}}]},
            {"type": "delete", "data": [{"id": "scene-relax", "type": "scene"}]}
        ])").array();
        store.applyEvents(events);

        QCOMPARE(changed.count(), 1);
        QCOMPARE(added.count(), 1);
        QCOMPARE(removed.count(), 1);
        QVERIFY(at(store.resource(QStringLiteral("light-2")), "on", "on").toBool());
        QCOMPARE(at(store.resource(QStringLiteral("light-2")), "dimming", "brightness").toDouble(), 40.0);
        QVERIFY(!store.contains(QStringLiteral("unknown")));
        QVERIFY(!store.contains(QStringLiteral("scene-relax")));
        QCOMPARE(store.scenesOf(QStringLiteral("room-bed")).size(), 2);
    }

    void colorConversions()
    {
        const QPointF red = HueColor::rgbToXy(QColor(Qt::red));
        QVERIFY(qAbs(red.x() - 0.7006) < 0.001);
        QVERIFY(qAbs(red.y() - 0.2993) < 0.001);

        const HueColor::Gamut gamut = HueColor::defaultGamut();
        QVERIFY(!HueColor::inGamut(red, gamut));
        const QPointF clamped = HueColor::clampToGamut(red, gamut);
        QVERIFY(HueColor::inGamut(clamped, gamut) || qAbs(clamped.x() - gamut.red.x()) < 0.02);
        QVERIFY(qAbs(clamped.x() - gamut.red.x()) < 0.02);
        const QPointF white(0.3127, 0.329);
        QCOMPARE(HueColor::clampToGamut(white, gamut), white);

        const QColor orange(255, 128, 0);
        const QColor back = HueColor::xyToRgb(HueColor::rgbToXy(orange));
        QVERIFY(qAbs(back.hue() - orange.hue()) <= 3);

        const QColor cool = HueColor::mirekToRgb(153);
        const QColor warm = HueColor::mirekToRgb(500);
        QVERIFY(warm.red() > warm.blue());
        QVERIFY(cool.blue() > warm.blue());
    }

    void mdnsQuery()
    {
        QList<HueMdns::Record> records;
        QVERIFY(HueMdns::parse(HueMdns::buildQuery(), &records));
        QVERIFY(records.isEmpty());
    }

    void mdnsAnswer()
    {
        QByteArray packet;
        write16(&packet, 0);
        write16(&packet, 0x8400);
        write16(&packet, 0);  // questions
        write16(&packet, 1);  // answers
        write16(&packet, 0);
        write16(&packet, 3);  // additional

        const int service = packet.size();
        writeName(&packet, {"_hue", "_tcp", "local"});
        write16(&packet, HueMdns::PTR);
        write16(&packet, 1);
        write32(&packet, 120);
        QByteArray instance;
        writeName(&instance, {"Hue Bridge - 00CAFE"}, service);
        write16(&packet, quint16(instance.size()));
        const int instanceOffset = packet.size();
        packet += instance;

        writeName(&packet, {}, instanceOffset);
        write16(&packet, HueMdns::SRV);
        write16(&packet, 0x8001);
        write32(&packet, 120);
        QByteArray srv;
        write16(&srv, 0);
        write16(&srv, 0);
        write16(&srv, 443);
        writeName(&srv, {"ecb5fafffe00cafe", "local"});
        write16(&packet, quint16(srv.size()));
        packet += srv;

        writeName(&packet, {}, instanceOffset);
        write16(&packet, HueMdns::TXT);
        write16(&packet, 0x8001);
        write32(&packet, 120);
        QByteArray txt;
        for (const QByteArray &entry : {QByteArray("bridgeid=001788FFFE00CAFE"), QByteArray("modelid=BSB002")}) {
            txt.append(char(entry.size()));
            txt.append(entry);
        }
        write16(&packet, quint16(txt.size()));
        packet += txt;

        writeName(&packet, {"ecb5fafffe00cafe", "local"});
        write16(&packet, HueMdns::A);
        write16(&packet, 0x8001);
        write32(&packet, 120);
        write16(&packet, 4);
        packet += QByteArray("\xc0\xa8\x01\x2a", 4);

        QList<HueMdns::Record> records;
        QVERIFY(HueMdns::parse(packet, &records));
        QCOMPARE(records.size(), 4);
        QCOMPARE(records.at(0).target, QStringLiteral("Hue Bridge - 00CAFE._hue._tcp.local"));
        QCOMPARE(records.at(1).name, QStringLiteral("Hue Bridge - 00CAFE._hue._tcp.local"));

        HueMdns::Bridge bridge;
        QVERIFY(HueMdns::findBridge(records, &bridge));
        QCOMPARE(bridge.id, QStringLiteral("001788fffe00cafe"));
        QCOMPARE(bridge.address, QHostAddress(QStringLiteral("192.168.1.42")));
        QCOMPARE(int(bridge.port), 443);

        // Truncated packets fail without crashing.
        for (int size = 0; size < packet.size(); ++size) {
            QList<HueMdns::Record> partial;
            HueMdns::parse(packet.left(size), &partial);
        }
    }

    void bridgeConnectsAndFollowsEvents()
    {
        FakeHueBridge fake;
        QNetworkAccessManager nam;
        HueResourceStore store;
        HueBridge bridge(&nam, &store);
        HueBridgeConfig config;
        config.baseUrl = fake.url();
        config.appKey = QStringLiteral("secret");
        bridge.setConfig(config);
        bridge.start();

        QTRY_COMPARE(bridge.state(), HueBridge::Connected);
        QCOMPARE(fake.requests.first().path, QByteArray("/eventstream/clip/v2"));
        QCOMPARE(fake.requests.first().appKey, QByteArray("secret"));
        QVERIFY(store.contains(QStringLiteral("light-1")));

        fake.push(updateEvent(json(R"({"id": "light-2", "type": "light", "on": {"on": true}})")));
        QTRY_VERIFY(at(store.resource(QStringLiteral("light-2")), "on", "on").toBool());
    }

    void bridgeReconnectsAndReloads()
    {
        FakeHueBridge fake;
        QNetworkAccessManager nam;
        HueResourceStore store;
        HueBridge bridge(&nam, &store);
        bridge.setReconnectDelays(10, 100);
        HueBridgeConfig config;
        config.baseUrl = fake.url();
        config.appKey = QStringLiteral("secret");
        bridge.setConfig(config);
        bridge.start();
        QTRY_COMPARE(bridge.state(), HueBridge::Connected);

        // Changed while we weren't listening: the reload picks it up.
        QJsonArray data = fake.resources.value(QStringLiteral("data")).toArray();
        data.append(json(R"({"id": "light-9", "type": "light", "metadata": {"name": "New"}})"));
        fake.resources.insert(QStringLiteral("data"), data);
        fake.closeStreams();

        QTRY_COMPARE(fake.count("GET", "/clip/v2/resource"), 2);
        QTRY_COMPARE(bridge.state(), HueBridge::Connected);
        QVERIFY(store.contains(QStringLiteral("light-9")));
    }

    void bridgeUnauthorized()
    {
        FakeHueBridge fake;
        fake.streamStatus = 403;
        QNetworkAccessManager nam;
        HueResourceStore store;
        HueBridge bridge(&nam, &store);
        HueBridgeConfig config;
        config.baseUrl = fake.url();
        config.appKey = QStringLiteral("revoked");
        bridge.setConfig(config);
        bridge.start();
        QTRY_COMPARE(bridge.state(), HueBridge::Unauthorized);
        QTest::qWait(100);
        QCOMPARE(fake.count("GET", "/eventstream"), 1); // no retrying
    }

    void bridgeWithoutV2()
    {
        FakeHueBridge fake;
        fake.streamStatus = 404;
        QNetworkAccessManager nam;
        HueResourceStore store;
        HueBridge bridge(&nam, &store);
        bridge.setReconnectDelays(1000, 1000);
        QSignalSpy errors(&bridge, &HueBridge::errorOccurred);
        HueBridgeConfig config;
        config.baseUrl = fake.url();
        config.appKey = QStringLiteral("secret");
        bridge.setConfig(config);
        bridge.start();
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("too old")));
        QCOMPARE(bridge.state(), HueBridge::WaitingToReconnect);
    }

    void queueCoalesces()
    {
        FakeHueBridge fake;
        QNetworkAccessManager nam;
        HueResourceStore store;
        HueBridge bridge(&nam, &store);
        HueBridgeConfig config;
        config.baseUrl = fake.url();
        config.appKey = QStringLiteral("secret");
        bridge.setConfig(config);
        HueCommandQueue queue(&bridge);
        queue.setIntervals(50, 300);

        const QString light = QStringLiteral("light-1");
        for (int i = 1; i <= 5; ++i) {
            QJsonObject dimming;
            dimming.insert(QStringLiteral("brightness"), i * 10);
            QJsonObject body;
            body.insert(QStringLiteral("dimming"), dimming);
            queue.enqueue(QStringLiteral("light"), light, body);
        }
        queue.enqueue(QStringLiteral("light"), light, json(R"({"on": {"on": true}})"));
        QTRY_VERIFY(queue.isIdle());

        const QList<FakeHueBridge::Request> puts = fake.puts();
        QCOMPARE(puts.size(), 2); // the first right away, the rest merged into one
        QCOMPARE(puts.at(0).path, QByteArray("/clip/v2/resource/light/light-1"));
        const QJsonObject last = QJsonDocument::fromJson(puts.at(1).body).object();
        QCOMPARE(at(last, "dimming", "brightness").toInt(), 50);
        QVERIFY(at(last, "on", "on").toBool());
    }

    void queuePacesGroups()
    {
        FakeHueBridge fake;
        QNetworkAccessManager nam;
        HueResourceStore store;
        HueBridge bridge(&nam, &store);
        HueBridgeConfig config;
        config.baseUrl = fake.url();
        config.appKey = QStringLiteral("secret");
        bridge.setConfig(config);
        HueCommandQueue queue(&bridge);
        queue.setIntervals(10, 300);

        const QJsonObject on = json(R"({"on": {"on": true}})");
        queue.enqueue(QStringLiteral("grouped_light"), QStringLiteral("gl-living"), on);
        QTRY_VERIFY(queue.isIdle());
        QElapsedTimer timer;
        timer.start();
        queue.enqueue(QStringLiteral("grouped_light"), QStringLiteral("gl-living"), on);
        // Another group isn't held up by it.
        queue.enqueue(QStringLiteral("grouped_light"), QStringLiteral("gl-bed"), on);
        QTRY_COMPARE(fake.puts().size(), 2);
        QVERIFY(fake.puts().at(1).path.endsWith("gl-bed"));
        QTRY_VERIFY(queue.isIdle());
        QCOMPARE(fake.puts().size(), 3);
        QVERIFY(timer.elapsed() >= 200);
    }

    void pairingWaitsForButton()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"error": {"type": 101, "address": "", "description": "link button not pressed"}}])")
                         << QByteArray(R"([{"success": {"username": "new-key", "clientkey": "abc"}}])");
        QNetworkAccessManager nam;
        HuePairing pairing(&nam);
        pairing.setPollInterval(20);
        QSignalSpy waiting(&pairing, &HuePairing::waitingForButton);
        QSignalSpy paired(&pairing, &HuePairing::paired);
        pairing.start(fake.url(), QStringLiteral("lampje#test"));
        QTRY_COMPARE(paired.count(), 1);
        QCOMPARE(waiting.count(), 1);
        QCOMPARE(paired.first().first().toString(), QStringLiteral("new-key"));
        const QJsonObject body = QJsonDocument::fromJson(fake.requests.first().body).object();
        QCOMPARE(body.value(QStringLiteral("devicetype")).toString(), QStringLiteral("lampje#test"));
        QVERIFY(!body.contains(QStringLiteral("generatekey")));
    }

    void pairingTimesOut()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"error": {"type": 101, "description": "link button not pressed"}}])");
        QNetworkAccessManager nam;
        HuePairing pairing(&nam);
        pairing.setPollInterval(20);
        pairing.setTimeout(150);
        QSignalSpy failed(&pairing, &HuePairing::failed);
        pairing.start(fake.url(), QStringLiteral("lampje#test"));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!pairing.isRunning());
    }

    void discoveryProbe()
    {
        FakeHueBridge fake;
        QNetworkAccessManager nam;
        HueDiscovery discovery(&nam);
        QSignalSpy finished(&discovery, &HueDiscovery::probeFinished);
        discovery.probe(fake.host());
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(discovery.bridges().size(), 1);
        const QVariantMap bridge = discovery.bridges().first().toMap();
        QCOMPARE(bridge.value(QStringLiteral("id")).toString(), QStringLiteral("001788fffe00cafe"));
        QCOMPARE(bridge.value(QStringLiteral("name")).toString(), QStringLiteral("Fake bridge"));
        QVERIFY(bridge.value(QStringLiteral("supported")).toBool());

        QCOMPARE(HueDiscovery::baseUrlFor(QStringLiteral("192.168.1.2")),
                 QUrl(QStringLiteral("https://192.168.1.2")));
        QCOMPARE(HueDiscovery::baseUrlFor(QStringLiteral(" http://10.0.2.2:8080/ ")),
                 QUrl(QStringLiteral("http://10.0.2.2:8080")));
    }

    void roomModel()
    {
        HueResourceStore store;
        store.reset(fixtureData());
        HueRoomModel model;
        model.setStore(&store);
        QCOMPARE(model.rowCount(), 3);
        auto value = [&model](int row, int role) { return model.data(model.index(row), role); };
        QCOMPARE(value(0, HueRoomModel::NameRole).toString(), QStringLiteral("Bedroom"));
        QCOMPARE(value(1, HueRoomModel::NameRole).toString(), QStringLiteral("Living room"));
        QCOMPARE(value(2, HueRoomModel::KindRole).toString(), QStringLiteral("zone"));
        QCOMPARE(value(1, HueRoomModel::LightCountRole).toInt(), 2);
        QCOMPARE(value(1, HueRoomModel::GroupedLightIdRole).toString(), QStringLiteral("gl-living"));

        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        store.applyUpdate(json(R"({"id": "gl-living", "dimming": {"brightness": 10}})"));
        QCOMPARE(reset.count(), 0);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().value<QModelIndex>().row(), 1);
        QCOMPARE(value(1, HueRoomModel::BrightnessRole).toDouble(), 10.0);

        store.applyEvents(QJsonDocument::fromJson(R"([{"type": "delete", "data": [{"id": "zone-reading"}]}])").array());
        QCOMPARE(model.rowCount(), 2);
    }

    void lightAndSceneModels()
    {
        HueResourceStore store;
        store.reset(fixtureData());
        HueLightModel lights;
        lights.setStore(&store);
        QCOMPARE(lights.rowCount(), 4); // three lights and the three-spot lamp
        lights.setGroupId(QStringLiteral("room-living"));
        QCOMPARE(lights.rowCount(), 2);
        QCOMPARE(lights.idAt(0), QStringLiteral("light-2")); // Ceiling, Floor lamp
        auto value = [&lights](int row, int role) { return lights.data(lights.index(row), role); };
        QVERIFY(!value(0, HueLightModel::SupportsColorRole).toBool());
        QVERIFY(value(1, HueLightModel::SupportsColorRole).toBool());
        QCOMPARE(value(1, HueLightModel::MirekMaxRole).toInt(), 500);

        lights.setGroupId(QString());
        lights.setLightId(QStringLiteral("light-3"));
        QCOMPARE(lights.rowCount(), 1);
        QVERIFY(!value(0, HueLightModel::ReachableRole).toBool());
        const QColor color = value(0, HueLightModel::DisplayColorRole).value<QColor>();
        QVERIFY(color.red() > color.blue()); // 400 mirek is warm

        // Reachability comes from another resource, but still updates the row.
        QSignalSpy changed(&lights, &QAbstractItemModel::dataChanged);
        store.applyUpdate(json(R"({"id": "zc-3", "status": "connected"})"));
        QCOMPARE(changed.count(), 1);
        QVERIFY(value(0, HueLightModel::ReachableRole).toBool());

        HueSceneModel scenes;
        scenes.setStore(&store);
        scenes.setGroupId(QStringLiteral("room-living"));
        QCOMPARE(scenes.rowCount(), 2);
        QCOMPARE(scenes.data(scenes.index(1), HueSceneModel::NameRole).toString(), QStringLiteral("Sunset"));
        QVERIFY(scenes.data(scenes.index(1), HueSceneModel::ActiveRole).toBool());
        QVERIFY(!scenes.data(scenes.index(0), HueSceneModel::ActiveRole).toBool());
    }

    void fixtures()
    {
        HueResourceStore store;
        store.reset(fixtureData());
        const QString fixture = HueResourceStore::fixtureId(QStringLiteral("929003046301"), QStringLiteral("room-bed"));
        QCOMPARE(store.fixtureProductOf(QStringLiteral("light-spot-1")), QStringLiteral("929003046301"));
        QVERIFY(store.fixtureProductOf(QStringLiteral("light-3")).isEmpty());
        QCOMPARE(store.lightsOfFixture(fixture).size(), 3);
        QCOMPARE(store.name(fixture), QStringLiteral("Hue Runner spot"));

        HueLightModel lights;
        lights.setStore(&store);
        lights.setGroupId(QStringLiteral("room-bed"));
        QCOMPARE(lights.rowCount(), 2);
        QCOMPARE(lights.idAt(1), fixture); // Bedside, Hue Runner spot
        auto value = [&lights](int row, int role) { return lights.data(lights.index(row), role); };
        QCOMPARE(value(1, HueLightModel::NameRole).toString(), QStringLiteral("Hue Runner spot"));
        QCOMPARE(value(1, HueLightModel::LightCountRole).toInt(), 3);
        QVERIFY(value(1, HueLightModel::OnRole).toBool());
        QCOMPARE(value(1, HueLightModel::BrightnessRole).toDouble(), 40.0); // of the two that are on
        QVERIFY(value(1, HueLightModel::SupportsTemperatureRole).toBool());
        QVERIFY(!value(1, HueLightModel::SupportsColorRole).toBool());

        // A change to one spot updates the lamp's row.
        QSignalSpy changed(&lights, &QAbstractItemModel::dataChanged);
        store.applyUpdate(json(R"({"id": "light-spot-1", "on": {"on": false}})"));
        store.applyUpdate(json(R"({"id": "light-spot-2", "on": {"on": false}})"));
        QCOMPARE(changed.count(), 2);
        QVERIFY(!value(1, HueLightModel::OnRole).toBool());

        // One spot alone is just a light.
        QCOMPARE(store.withFixtures(QStringList(QStringLiteral("light-spot-1")), QString()),
                 QStringList(QStringLiteral("light-spot-1")));

        // The lamp's page: the lamp itself, and its spots.
        HueLightModel lamp;
        lamp.setStore(&store);
        lamp.setLightId(fixture);
        QCOMPARE(lamp.rowCount(), 1);
        HueLightModel spots;
        spots.setStore(&store);
        spots.setGroupFixtures(false);
        spots.setLightId(fixture);
        QCOMPARE(spots.rowCount(), 3);
        QCOMPARE(spots.idAt(0), QStringLiteral("light-spot-1"));
    }

    void sensorModel()
    {
        HueResourceStore store;
        store.reset(fixtureData());
        HueSensorModel sensors;
        sensors.setStore(&store);
        QCOMPARE(sensors.rowCount(), 2); // Hallway, Hue dimmer switch 1; not the lamps
        auto value = [&sensors](int row, int role) { return sensors.data(sensors.index(row), role); };

        QCOMPARE(value(0, HueSensorModel::KindRole).toString(), QStringLiteral("motion"));
        QCOMPARE(value(0, HueSensorModel::MotionIdRole).toString(), QStringLiteral("motion-1"));
        QVERIFY(value(0, HueSensorModel::MotionEnabledRole).toBool());
        QVERIFY(!value(0, HueSensorModel::MotionRole).toBool());
        QCOMPARE(value(0, HueSensorModel::MotionChangedRole).toDateTime().toUTC(),
                 QDateTime(QDate(2026, 10, 30), QTime(18, 8, 6, 5), Qt::UTC));
        QCOMPARE(value(0, HueSensorModel::TemperatureRole).toDouble(), 21.5);
        QVERIFY(qAbs(value(0, HueSensorModel::LightLevelRole).toDouble() - 10.0) < 0.01);
        QCOMPARE(value(0, HueSensorModel::BatteryLevelRole).toInt(), 12);
        QVERIFY(value(0, HueSensorModel::BatteryLowRole).toBool());

        QCOMPARE(value(1, HueSensorModel::KindRole).toString(), QStringLiteral("switch"));
        QVERIFY(!value(1, HueSensorModel::TemperatureRole).isValid());
        QCOMPARE(value(1, HueSensorModel::LastButtonRole).toInt(), 2);
        QCOMPARE(value(1, HueSensorModel::LastButtonEventRole).toString(), QStringLiteral("long_press"));

        // A button press arrives as an event on one button.
        QSignalSpy changed(&sensors, &QAbstractItemModel::dataChanged);
        store.applyUpdate(json(R"({"id": "button-1", "button": {"button_report": {"updated": "2026-10-30T10:00:00.000Z", "event": "initial_press"}}})"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(value(1, HueSensorModel::LastButtonRole).toInt(), 1);
        QCOMPARE(value(1, HueSensorModel::LastButtonEventRole).toString(), QStringLiteral("initial_press"));
    }

    void scheduleModel()
    {
        HueResourceStore store;
        store.reset(fixtureData());
        HueScheduleModel schedules;
        schedules.setStore(&store);
        // Evening lights, Tea, Weekday mornings; not the switch's behaviour.
        QCOMPARE(schedules.rowCount(), 3);
        auto value = [&schedules](int row, int role) { return schedules.data(schedules.index(row), role); };

        QCOMPARE(schedules.idAt(2), QStringLiteral("schedule-wake"));
        QCOMPARE(value(2, HueScheduleModel::KindRole).toString(), QStringLiteral("wake_up"));
        QCOMPARE(value(2, HueScheduleModel::ScriptNameRole).toString(), QStringLiteral("Wake up"));
        QVERIFY(value(2, HueScheduleModel::EnabledRole).toBool());
        QCOMPARE(value(2, HueScheduleModel::TimeTypeRole).toString(), QStringLiteral("time"));
        QCOMPARE(value(2, HueScheduleModel::HourRole).toInt(), 7);
        QCOMPARE(value(2, HueScheduleModel::MinuteRole).toInt(), 15);
        QCOMPARE(value(2, HueScheduleModel::EndHourRole).toInt(), -1);
        // In week order, whatever the bridge's.
        QCOMPARE(value(2, HueScheduleModel::DaysRole).toStringList(),
                 QStringList() << QStringLiteral("monday") << QStringLiteral("tuesday") << QStringLiteral("wednesday")
                               << QStringLiteral("thursday") << QStringLiteral("friday"));
        QCOMPARE(value(2, HueScheduleModel::PlacesRole).toStringList(), QStringList(QStringLiteral("Bedroom")));
        QVERIFY(value(2, HueScheduleModel::EditableRole).toBool());
        QVERIFY(value(2, HueScheduleModel::ErrorRole).toString().isEmpty());

        QCOMPARE(value(0, HueScheduleModel::KindRole).toString(), QStringLiteral("time_based_light"));
        QCOMPARE(value(0, HueScheduleModel::TimeTypeRole).toString(), QStringLiteral("sunset"));
        QCOMPARE(value(0, HueScheduleModel::OffsetRole).toInt(), -30);
        QCOMPARE(value(0, HueScheduleModel::HourRole).toInt(), -1);
        QCOMPARE(value(0, HueScheduleModel::EndHourRole).toInt(), 23);
        QCOMPARE(value(0, HueScheduleModel::EndMinuteRole).toInt(), 0);
        QCOMPARE(value(0, HueScheduleModel::DaysRole).toStringList().size(), 7);
        QCOMPARE(value(0, HueScheduleModel::PlacesRole).toStringList(),
                 QStringList() << QStringLiteral("Reading corner") << QStringLiteral("Whole home"));
        QVERIFY(!value(0, HueScheduleModel::EditableRole).toBool());
        QCOMPARE(value(0, HueScheduleModel::ErrorRole).toString(), QStringLiteral("scene not found"));

        QCOMPARE(value(1, HueScheduleModel::KindRole).toString(), QStringLiteral("timer"));
        QVERIFY(!value(1, HueScheduleModel::EnabledRole).toBool());
        QCOMPARE(value(1, HueScheduleModel::DurationRole).toInt(), 240);
        QCOMPARE(value(1, HueScheduleModel::TimeTypeRole).toString(), QString());
        QVERIFY(value(1, HueScheduleModel::DaysRole).toStringList().isEmpty());

        // Renaming the room it's for changes the row.
        QSignalSpy changed(&schedules, &QAbstractItemModel::dataChanged);
        store.applyUpdate(json(R"({"id": "room-bed", "type": "room", "metadata": {"name": "Guest room"}})"));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(value(2, HueScheduleModel::PlacesRole).toStringList(), QStringList(QStringLiteral("Guest room")));
    }

    void clientSchedules()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"success": {"username": "client-key"}}])");
        QTemporaryDir dir;
        HueClient client(dir.path() + QStringLiteral("/lampje.conf"));
        client.pair(fake.host(), QStringLiteral("001788fffe00cafe"), QStringLiteral("Fake bridge"));
        QTRY_COMPARE(client.state(), HueClient::Connected);

        client.setEnabled(QStringLiteral("schedule-timer"), true);
        QVERIFY(client.store()->resource(QStringLiteral("schedule-timer")).value(QStringLiteral("enabled")).toBool());
        QTRY_COMPARE(fake.puts().size(), 1);
        QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/behavior_instance/schedule-timer"));
        QCOMPARE(fake.puts().last().body, QByteArray(R"({"enabled":true})"));

        // Saturdays at half past eight; the rest of the configuration goes along.
        client.setScheduleTime(QStringLiteral("schedule-wake"), 8, 30,
                               QStringList() << QStringLiteral("saturday") << QStringLiteral("bogus"));
        const QJsonObject local = client.store()->resource(QStringLiteral("schedule-wake"));
        QCOMPARE(HueJson::at(local, {QStringLiteral("configuration"), QStringLiteral("when"), QStringLiteral("time_point"),
                                     QStringLiteral("time"), QStringLiteral("hour")}).toInt(), 8);
        QTRY_COMPARE(fake.puts().size(), 2);
        QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/behavior_instance/schedule-wake"));
        const QJsonObject configuration = QJsonDocument::fromJson(fake.puts().last().body).object()
                .value(QStringLiteral("configuration")).toObject();
        QCOMPARE(at(configuration, "when", "recurrence_days").toArray(), QJsonArray() << QStringLiteral("saturday"));
        QCOMPARE(HueJson::at(configuration, {QStringLiteral("when"), QStringLiteral("time_point"),
                                             QStringLiteral("time"), QStringLiteral("minute")}).toInt(), 30);
        QCOMPARE(at(configuration, "fade_in_duration", "seconds").toInt(), 1800);
        QCOMPARE(configuration.value(QStringLiteral("where")).toArray().size(), 1);

        // No days: once. Gone from the store too, not just left over from before.
        client.setScheduleTime(QStringLiteral("schedule-wake"), 6, 0, QStringList());
        QVERIFY(!HueJson::at(client.store()->resource(QStringLiteral("schedule-wake")),
                             {QStringLiteral("configuration"), QStringLiteral("when")}).toObject()
                .contains(QStringLiteral("recurrence_days")));
        QTRY_COMPARE(fake.puts().size(), 3);
        QVERIFY(!fake.puts().last().body.contains("recurrence_days"));

        // Starting at sunset, and a timer: no time to change.
        client.setScheduleTime(QStringLiteral("schedule-evening"), 20, 0, QStringList());
        client.setScheduleTime(QStringLiteral("schedule-timer"), 20, 0, QStringList());
        client.setScheduleTime(QStringLiteral("schedule-wake"), 24, 0, QStringList());
        QTest::qWait(100);
        QCOMPARE(fake.puts().size(), 3);

        client.deleteSchedule(QStringLiteral("schedule-timer"));
        QTRY_VERIFY(!client.store()->contains(QStringLiteral("schedule-timer")));
        QCOMPARE(fake.withMethod("DELETE").last().path, QByteArray("/clip/v2/resource/behavior_instance/schedule-timer"));
    }

    void clientScenes()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"success": {"username": "client-key"}}])");
        QTemporaryDir dir;
        HueClient client(dir.path() + QStringLiteral("/lampje.conf"));
        client.pair(fake.host(), QStringLiteral("001788fffe00cafe"), QStringLiteral("Fake bridge"));
        QTRY_COMPARE(client.state(), HueClient::Connected);

        // The living room: the floor lamp on in a colour, the ceiling off.
        QSignalSpy created(&client, &HueClient::sceneCreated);
        client.createScene(QStringLiteral("room-living"), QStringLiteral("  Evening  "));
        QTRY_COMPARE(created.count(), 1);
        QCOMPARE(created.first().first().toString(), QStringLiteral("scene-created"));
        const FakeHueBridge::Request post = fake.withMethod("POST").last();
        QCOMPARE(post.path, QByteArray("/clip/v2/resource/scene"));
        const QJsonObject scene = QJsonDocument::fromJson(post.body).object();
        QCOMPARE(at(scene, "metadata", "name").toString(), QStringLiteral("Evening"));
        QCOMPARE(at(scene, "group", "rtype").toString(), QStringLiteral("room"));
        QJsonObject floor, ceiling;
        for (const QJsonValue &value : scene.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString target = at(entry, "target", "rid").toString();
            (target == QLatin1String("light-1") ? floor : ceiling) = entry.value(QStringLiteral("action")).toObject();
        }
        QVERIFY(at(floor, "on", "on").toBool());
        QCOMPARE(at(floor, "dimming", "brightness").toDouble(), 80.0);
        QCOMPARE(HueJson::at(floor, {QStringLiteral("color"), QStringLiteral("xy"), QStringLiteral("x")}).toDouble(), 0.6);
        QVERIFY(!floor.contains(QStringLiteral("color_temperature")));
        QVERIFY(!at(ceiling, "on", "on").toBool());
        QCOMPARE(ceiling.keys(), QStringList(QStringLiteral("on")));

        client.renameScene(QStringLiteral("scene-relax"), QStringLiteral("Unwind"));
        QCOMPARE(client.store()->name(QStringLiteral("scene-relax")), QStringLiteral("Unwind"));
        QTRY_COMPARE(fake.puts().size(), 1);
        QCOMPARE(fake.puts().last().body, QByteArray(R"({"metadata":{"name":"Unwind"}})"));

        client.storeScene(QStringLiteral("scene-relax"));
        QTRY_COMPARE(fake.puts().size(), 2);
        const QJsonObject stored = QJsonDocument::fromJson(fake.puts().last().body).object();
        QCOMPARE(stored.value(QStringLiteral("actions")).toArray().size(), 2);

        client.deleteScene(QStringLiteral("scene-relax"));
        QTRY_VERIFY(!client.store()->contains(QStringLiteral("scene-relax")));
        QCOMPARE(fake.withMethod("DELETE").last().path, QByteArray("/clip/v2/resource/scene/scene-relax"));
    }

    void clientZones()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"success": {"username": "client-key"}}])");
        QTemporaryDir dir;
        HueClient client(dir.path() + QStringLiteral("/lampje.conf"));
        client.pair(fake.host(), QStringLiteral("001788fffe00cafe"), QStringLiteral("Fake bridge"));
        QTRY_COMPARE(client.state(), HueClient::Connected);

        // A light from each of two rooms, and a whole three-spot lamp.
        const QString runner = HueResourceStore::fixtureId(QStringLiteral("929003046301"), QString());
        QCOMPARE(client.resolveLights(runner).size(), 3);
        client.createZone(QStringLiteral(" Kids "), QStringLiteral("kids_bedroom"),
                          QStringList() << QStringLiteral("light-2") << QStringLiteral("light-3") << runner
                                        << QStringLiteral("light-2"));
        QTRY_COMPARE(fake.withMethod("POST").size(), 2); // pairing, then the zone
        const FakeHueBridge::Request post = fake.withMethod("POST").last();
        QCOMPARE(post.path, QByteArray("/clip/v2/resource/zone"));
        const QJsonObject zone = QJsonDocument::fromJson(post.body).object();
        QCOMPARE(zone.value(QStringLiteral("type")).toString(), QStringLiteral("zone"));
        QCOMPARE(at(zone, "metadata", "name").toString(), QStringLiteral("Kids"));
        QCOMPARE(at(zone, "metadata", "archetype").toString(), QStringLiteral("kids_bedroom"));
        const QJsonArray children = zone.value(QStringLiteral("children")).toArray();
        QCOMPARE(children.size(), 5); // no duplicates
        for (const QJsonValue &child : children)
            QCOMPARE(child.toObject().value(QStringLiteral("rtype")).toString(), QStringLiteral("light"));

        // Nothing to switch: not created.
        client.createZone(QStringLiteral("Empty"), QString(), QStringList());
        QTest::qWait(100);
        QCOMPARE(fake.withMethod("POST").size(), 2);

        client.updateZone(QStringLiteral("zone-reading"), QStringLiteral("Reading"), QStringLiteral("reading"),
                          QStringList() << QStringLiteral("light-1") << QStringLiteral("light-3"));
        QCOMPARE(client.lightsOf(QStringLiteral("zone-reading")).size(), 2); // right away
        QCOMPARE(client.store()->name(QStringLiteral("zone-reading")), QStringLiteral("Reading"));
        QTRY_COMPARE(fake.puts().size(), 1);
        QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/zone/zone-reading"));

        client.deleteZone(QStringLiteral("zone-reading"));
        QTRY_VERIFY(!client.store()->contains(QStringLiteral("zone-reading")));
        QVERIFY(!client.store()->contains(QStringLiteral("gl-reading")));
        QCOMPARE(fake.withMethod("DELETE").last().path, QByteArray("/clip/v2/resource/zone/zone-reading"));
    }

    void clientRenames()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"success": {"username": "client-key"}}])");
        QTemporaryDir dir;
        HueClient client(dir.path() + QStringLiteral("/lampje.conf"));
        client.pair(fake.host(), QStringLiteral("001788fffe00cafe"), QStringLiteral("Fake bridge"));
        QTRY_COMPARE(client.state(), HueClient::Connected);

        client.renameLight(QStringLiteral("light-1"), QStringLiteral(" Reading lamp "));
        QCOMPARE(client.store()->name(QStringLiteral("light-1")), QStringLiteral("Reading lamp"));
        QCOMPARE(client.store()->name(QStringLiteral("dev-1")), QStringLiteral("Reading lamp"));
        QTRY_COMPARE(fake.puts().size(), 2);
        QStringList paths;
        for (const FakeHueBridge::Request &put : fake.puts()) {
            paths << QString::fromLatin1(put.path);
            QCOMPARE(put.body, QByteArray(R"({"metadata":{"name":"Reading lamp"}})"));
        }
        paths.sort();
        QCOMPARE(paths, QStringList() << QStringLiteral("/clip/v2/resource/device/dev-1")
                                      << QStringLiteral("/clip/v2/resource/light/light-1"));

        // Nameless: nothing happens.
        client.renameLight(QStringLiteral("light-1"), QStringLiteral("   "));

        client.updateRoom(QStringLiteral("room-bed"), QStringLiteral("Guest room"), QStringLiteral("guest_room"));
        QCOMPARE(client.store()->name(QStringLiteral("room-bed")), QStringLiteral("Guest room"));
        QTRY_COMPARE(fake.puts().size(), 3);
        const QJsonObject room = QJsonDocument::fromJson(fake.puts().last().body).object();
        QCOMPARE(at(room, "metadata", "archetype").toString(), QStringLiteral("guest_room"));
        QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/room/room-bed"));
    }

    void clientCoverActions()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"success": {"username": "client-key"}}])");
        QTemporaryDir dir;
        const QString settings = dir.path() + QStringLiteral("/lampje.conf");
        {
            HueClient client(settings);
            client.pair(fake.host(), QStringLiteral("001788fffe00cafe"), QStringLiteral("Fake bridge"));
            QTRY_COMPARE(client.state(), HueClient::Connected);

            QCOMPARE(client.coverActions().size(), 2);
            QCOMPARE(client.coverActions().at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("alloff"));
            QCOMPARE(client.coverActions().at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("none"));

            QSignalSpy changed(&client, &HueClient::coverActionsChanged);
            client.setCoverAction(0, QStringLiteral("group"), QStringLiteral("room-living"));
            client.setCoverAction(1, QStringLiteral("scene"), QStringLiteral("scene-relax"));
            QCOMPARE(changed.count(), 2);
            const QVariantMap room = client.coverActions().at(0).toMap();
            QCOMPARE(room.value(QStringLiteral("name")).toString(), QStringLiteral("Living room"));
            QCOMPARE(room.value(QStringLiteral("archetype")).toString(), QStringLiteral("living_room"));
            const QVariantMap scene = client.coverActions().at(1).toMap();
            QCOMPARE(scene.value(QStringLiteral("name")).toString(), QStringLiteral("Relax"));
            QCOMPARE(scene.value(QStringLiteral("groupName")).toString(), QStringLiteral("Living room"));

            // The living room is on, so its button switches it off; pressed again, on.
            client.runCoverAction(0);
            QTRY_COMPARE(fake.puts().size(), 1);
            QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/grouped_light/gl-living"));
            QCOMPARE(fake.puts().last().body, QByteArray(R"({"on":{"on":false}})"));
            client.runCoverAction(0);
            QTRY_COMPARE(fake.puts().size(), 2);
            QCOMPARE(fake.puts().last().body, QByteArray(R"({"on":{"on":true}})"));

            client.runCoverAction(1);
            QTRY_COMPARE(fake.puts().size(), 3);
            QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/scene/scene-relax"));

            // Renaming the room, or changing its kind, changes the button.
            changed.clear();
            client.store()->applyUpdate(json(R"({"id": "room-living", "metadata": {"name": "Lounge", "archetype": "office"}})"));
            QCOMPARE(changed.count(), 1);
            QCOMPARE(client.coverActions().at(0).toMap().value(QStringLiteral("archetype")).toString(), QStringLiteral("office"));
            QCOMPARE(client.coverActions().at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Lounge"));
            // Other changes don't bother the cover.
            client.store()->applyUpdate(json(R"({"id": "light-1", "on": {"on": true}})"));
            QCOMPARE(changed.count(), 1);

            // A scene that's deleted leaves no button behind.
            client.store()->remove(QStringLiteral("scene-relax"));
            QCOMPARE(client.coverActions().at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("none"));
        }
        {
            // Remembered.
            HueClient client(settings);
            QTRY_COMPARE(client.state(), HueClient::Connected);
            QCOMPARE(client.coverActions().at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("group"));
            client.forget();
            QCOMPARE(client.coverActions().at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("alloff"));
        }
    }

    void clientPairsAndControls()
    {
        FakeHueBridge fake;
        fake.pairReplies << QByteArray(R"([{"success": {"username": "client-key"}}])");
        QTemporaryDir dir;
        const QString settings = dir.path() + QStringLiteral("/lampje.conf");
        {
            HueClient client(settings);
            QVERIFY(!client.isConfigured());
            QCOMPARE(client.state(), HueClient::NotConfigured);
            QSignalSpy paired(&client, &HueClient::paired);
            client.pair(fake.host(), QStringLiteral("001788fffe00cafe"), QStringLiteral("Fake bridge"));
            QTRY_COMPARE(paired.count(), 1);
            QVERIFY(client.isConfigured());
            QCOMPARE(client.bridgeName(), QStringLiteral("Fake bridge"));
            QTRY_COMPARE(client.state(), HueClient::Connected);

            // The store follows right away; the bridge gets it through the queue.
            client.setBrightness(QStringLiteral("light-2"), 30);
            QCOMPARE(at(client.store()->resource(QStringLiteral("light-2")), "dimming", "brightness").toDouble(), 30.0);
            QVERIFY(at(client.store()->resource(QStringLiteral("light-2")), "on", "on").toBool());
            QTRY_COMPARE(fake.puts().size(), 1);
            QCOMPARE(fake.puts().first().appKey, QByteArray("client-key"));
            const QJsonObject body = QJsonDocument::fromJson(fake.puts().first().body).object();
            QCOMPARE(at(body, "dimming", "brightness").toDouble(), 30.0);

            client.setColor(QStringLiteral("light-1"), QColor(Qt::red));
            QTRY_COMPARE(fake.puts().size(), 2);
            const QJsonObject xy = HueJson::at(QJsonDocument::fromJson(fake.puts().last().body).object(),
                                               {QStringLiteral("color"), QStringLiteral("xy")}).toObject();
            const QPointF sent(xy.value(QStringLiteral("x")).toDouble(), xy.value(QStringLiteral("y")).toDouble());
            QVERIFY(qAbs(sent.x() - 0.69) < 0.01); // clamped to gamut C's red corner
            QVERIFY(!at(client.store()->resource(QStringLiteral("light-1")), "color_temperature", "mirek_valid").toBool());

            client.recallScene(QStringLiteral("scene-relax"));
            QTRY_COMPARE(fake.puts().size(), 3);
            QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/scene/scene-relax"));

            // A lamp with three spots: every spot gets it.
            client.setMirek(HueResourceStore::fixtureId(QStringLiteral("929003046301"), QStringLiteral("room-bed")), 250);
            QTRY_COMPARE(fake.puts().size(), 6);
            for (int i = 3; i < 6; ++i)
                QVERIFY(fake.puts().at(i).path.startsWith("/clip/v2/resource/light/light-spot-"));
            QCOMPARE(at(client.store()->resource(QStringLiteral("light-spot-3")), "color_temperature", "mirek").toInt(), 250);

            client.setEnabled(QStringLiteral("motion-1"), false);
            QTRY_COMPARE(fake.puts().size(), 7);
            QCOMPARE(fake.puts().last().path, QByteArray("/clip/v2/resource/motion/motion-1"));
            QCOMPARE(fake.puts().last().body, QByteArray(R"({"enabled":false})"));
        }
        {
            // Remembered, and connects by itself.
            HueClient client(settings);
            QVERIFY(client.isConfigured());
            QTRY_COMPARE(client.state(), HueClient::Connected);
            client.forget();
            QVERIFY(!client.isConfigured());
            QCOMPARE(client.state(), HueClient::NotConfigured);
            QVERIFY(client.store()->isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestLampje)
#include "tst_lampje.moc"
