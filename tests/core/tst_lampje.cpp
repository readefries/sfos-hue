// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fakehuebridge.h"

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
