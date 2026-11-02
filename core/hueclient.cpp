// SPDX-License-Identifier: AGPL-3.0-or-later
#include "hueclient.h"

#include "huecolor.h"
#include "huecommandqueue.h"
#include "huediscovery.h"
#include "huejson.h"
#include "huemodels.h"
#include "huepairing.h"
#include "localgroups.h"
#include "hueresourcestore.h"
#include "mqttclient.h"
#include "zigbee2mqtt.h"

#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

const QString kId = QStringLiteral("bridge/id");
const QString kHost = QStringLiteral("bridge/host");
const QString kName = QStringLiteral("bridge/name");
const QString kAppKey = QStringLiteral("bridge/appKey");
const QString kCert = QStringLiteral("bridge/certSha256");
const QString kMqttHost = QStringLiteral("mqtt/host");
const QString kMqttPort = QStringLiteral("mqtt/port");
const QString kMqttUser = QStringLiteral("mqtt/user");
const QString kMqttPassword = QStringLiteral("mqtt/password");
const QString kMqttBase = QStringLiteral("mqtt/baseTopic");

// Reconnect when coming back after this long; the stream might have died unnoticed.
const qint64 kStaleAfterMsecs = 60 * 1000;

QString defaultSettingsPath()
{
    // Sailjail only gives the app its own directory under ~/.config/<org>/<app>/, so not the
    // usual <app>.conf next to it.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/lampje.conf");
}

QJsonObject object(const QString &key, const QJsonValue &value)
{
    QJsonObject obj;
    obj.insert(key, value);
    return obj;
}

} // namespace

HueClient::HueClient(const QString &settingsPath, QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(settingsPath.isEmpty() ? defaultSettingsPath() : settingsPath,
                               QSettings::IniFormat, this))
    , m_nam(new QNetworkAccessManager(this))
    , m_store(new HueResourceStore(this))
    , m_bridge(new HueBridge(m_nam, m_store, this))
    , m_queue(new HueCommandQueue(m_bridge, this))
    , m_discovery(new HueDiscovery(m_nam, this))
    , m_pairing(new HuePairing(m_nam, this))
    , m_mqtt(new MqttClient(this))
    , m_zigbee(new Zigbee2Mqtt(m_mqtt, m_store, this))
    , m_local(new LocalGroups(m_settings, m_store, this))
{
    connect(m_mqtt, &MqttClient::stateChanged, this, [this](MqttClient::State state) {
        if (state == MqttClient::Connected)
            m_mqttError.clear();
        emit mqttStateChanged();
    });
    connect(m_mqtt, &MqttClient::errorOccurred, this, [this](const QString &error) {
        m_mqttError = error;
        emit mqttStateChanged();
    });
    connect(m_bridge, &HueBridge::stateChanged, this, &HueClient::updateState);
    // Cover actions show names and icons of rooms, zones and scenes, which only the store knows.
    connect(m_store, &HueResourceStore::resourcesReset, this, &HueClient::coverActionsChanged);
    auto coverActionChanged = [this](const QString &id) {
        for (int slot = 1; slot <= 2; ++slot) {
            if (m_settings->value(QStringLiteral("cover/action%1Id").arg(slot)).toString() == id) {
                emit coverActionsChanged();
                return;
            }
        }
    };
    connect(m_store, &HueResourceStore::resourceAdded, this, coverActionChanged);
    connect(m_store, &HueResourceStore::resourceChanged, this, coverActionChanged);
    connect(m_store, &HueResourceStore::resourceRemoved, this, coverActionChanged);
    connect(m_bridge, &HueBridge::errorOccurred, this, &HueClient::setLastError);
    connect(m_queue, &HueCommandQueue::commandFailed, this,
            [this](const QString &, const QString &, const QString &error) {
        setLastError(error);
        m_bridge->reload();
    });

    connect(m_pairing, &HuePairing::waitingForButton, this, [this](int secondsLeft) {
        setPairing(WaitingForButton, secondsLeft, QString());
    });
    connect(m_pairing, &HuePairing::failed, this, [this](const QString &error) {
        setPairing(PairingFailed, 0, error);
    });
    connect(m_pairing, &HuePairing::paired, this, [this](const QString &appKey, const QByteArray &cert) {
        m_settings->setValue(kId, m_pairingId);
        m_settings->setValue(kHost, m_pairingHost);
        m_settings->setValue(kName, m_pairingName);
        m_settings->setValue(kAppKey, appKey);
        m_settings->setValue(kCert, QString::fromLatin1(cert.toHex()));
        m_settings->sync();
        setPairing(PairingIdle, 0, QString());
        setLastError(QString());
        applyConfig();
        emit configuredChanged();
        emit paired();
    });

    m_local->apply();
    applyConfig();
    applyMqttConfig();
}

HueClient::~HueClient()
{
    m_bridge->stop();
    m_mqtt->stop();
}

bool HueClient::isConfigured() const
{
    return !m_settings->value(kAppKey).toString().isEmpty();
}

QString HueClient::bridgeName() const
{
    return m_settings->value(kName).toString();
}

QString HueClient::bridgeHost() const
{
    return m_settings->value(kHost).toString();
}

void HueClient::applyConfig()
{
    m_bridge->stop();
    m_queue->clear();
    m_store->clear(); // the bridge's; Zigbee devices stay
    if (isConfigured()) {
        HueBridgeConfig config;
        config.baseUrl = HueDiscovery::baseUrlFor(bridgeHost());
        config.appKey = m_settings->value(kAppKey).toString();
        config.certSha256 = QByteArray::fromHex(m_settings->value(kCert).toString().toLatin1());
        m_bridge->setConfig(config);
        m_bridge->start();
    } else {
        m_bridge->setConfig(HueBridgeConfig());
    }
    updateState();
}

void HueClient::updateState()
{
    State state = NotConfigured;
    if (isConfigured()) {
        switch (m_bridge->state()) {
        case HueBridge::Idle:
        case HueBridge::Connecting: state = Connecting; break;
        case HueBridge::Connected: state = Connected; break;
        case HueBridge::WaitingToReconnect: state = Reconnecting; break;
        case HueBridge::Unauthorized: state = Unauthorized; break;
        case HueBridge::Untrusted: state = Untrusted; break;
        }
    }
    if (state == Connected)
        setLastError(QString());
    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

void HueClient::pair(const QString &host, const QString &bridgeId, const QString &name)
{
    m_pairingHost = host.trimmed();
    m_pairingId = bridgeId;
    m_pairingName = name;
    setPairing(WaitingForButton, 60, QString());
    m_pairing->start(HueDiscovery::baseUrlFor(m_pairingHost), QStringLiteral("lampje#sailfishos"));
}

void HueClient::cancelPairing()
{
    m_pairing->cancel();
    setPairing(PairingIdle, 0, QString());
}

void HueClient::forget()
{
    // The key stays valid on the bridge; it can only be removed in the official app.
    m_settings->remove(QStringLiteral("bridge"));
    m_settings->remove(QStringLiteral("cover"));
    m_settings->sync();
    setLastError(QString());
    applyConfig();
    emit configuredChanged();
    emit coverActionsChanged();
}

void HueClient::reconnect()
{
    if (isConfigured())
        m_bridge->reconnectNow();
}

void HueClient::setApplicationActive(bool active)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!active) {
        m_inactiveSince = now;
        return;
    }
    if (m_inactiveSince > 0 && now - m_inactiveSince > kStaleAfterMsecs) {
        if (isConfigured())
            m_bridge->reconnectNow();
        m_mqtt->reconnectNow();
    }
    m_inactiveSince = 0;
}

void HueClient::command(const QString &target, const QJsonObject &body, const QJsonObject &optimistic)
{
    if (HueResourceStore::isFixtureId(target)) {
        for (const QString &light : m_store->resolveLights(target))
            command(light, body, optimistic);
        return;
    }
    const QString &id = target;
    const QString type = m_store->type(id);
    if (type.isEmpty())
        return;
    if (HueResourceStore::sourceOf(id) == Zigbee2Mqtt::Source) {
        // Only light commands; it updates the store itself, with only what the light can do.
        if (type == QLatin1String("light"))
            m_zigbee->command(id, body);
        return;
    }
    if (!HueResourceStore::sourceOf(id).isEmpty())
        return;
    QJsonObject local = HueJson::merged(body, optimistic);
    local.insert(QStringLiteral("id"), id);
    local.insert(QStringLiteral("type"), type);
    m_store->applyUpdate(local);
    m_queue->enqueue(type, id, body);
}

void HueClient::lightCommand(const QString &target, const QJsonObject &body, const QJsonObject &optimistic)
{
    const QString type = m_store->type(target);
    if (type == QLatin1String("room") || type == QLatin1String("zone")) {
        // The bridge's lights through its grouped_light if it has one; the rest one by one.
        const QString groupedLight = m_store->groupedLightOf(target);
        if (!groupedLight.isEmpty())
            command(groupedLight, body, optimistic);
        // Dimming doesn't switch on lights that only switch (as the slider of a room with a
        // dimmable lamp and an on/off switch would otherwise do).
        const bool dimming = body.contains(QStringLiteral("dimming"));
        for (const QString &light : m_store->lightsOf(target)) {
            if (dimming && !m_store->resource(light).value(QStringLiteral("dimming")).isObject())
                continue;
            if (groupedLight.isEmpty() || !isFromBridge(light))
                command(light, body, optimistic);
        }
        return;
    }
    command(target, body, optimistic);
}

void HueClient::setOn(const QString &id, bool on)
{
    lightCommand(id, object(QStringLiteral("on"), object(QStringLiteral("on"), on)));
}

void HueClient::setBrightness(const QString &id, double brightness)
{
    QJsonObject body = object(QStringLiteral("dimming"),
                              object(QStringLiteral("brightness"), qBound(1.0, brightness, 100.0)));
    // Dimming a light that's off leaves it off; nobody means that.
    body.insert(QStringLiteral("on"), object(QStringLiteral("on"), true));
    lightCommand(id, body);
}

void HueClient::setColor(const QString &id, const QColor &color)
{
    // Each spot of a fixture clamps to its own gamut.
    if (HueResourceStore::isFixtureId(id)) {
        for (const QString &light : m_store->resolveLights(id))
            setColor(light, color);
        return;
    }
    const QJsonObject resource = m_store->resource(id);
    QPointF xy = HueColor::rgbToXy(color);
    const QJsonValue gamut = HueJson::at(resource, {QStringLiteral("color"), QStringLiteral("gamut")});
    if (gamut.isObject())
        xy = HueColor::clampToGamut(xy, HueColor::gamutFromJson(gamut.toObject()));
    QJsonObject point;
    point.insert(QStringLiteral("x"), qRound(xy.x() * 10000) / 10000.0);
    point.insert(QStringLiteral("y"), qRound(xy.y() * 10000) / 10000.0);
    QJsonObject body = object(QStringLiteral("color"), object(QStringLiteral("xy"), point));
    body.insert(QStringLiteral("on"), object(QStringLiteral("on"), true));
    // The lamp leaves colour temperature mode; the stream says so too, but later.
    lightCommand(id, body, object(QStringLiteral("color_temperature"), object(QStringLiteral("mirek_valid"), false)));
}

void HueClient::setMirek(const QString &id, int mirek)
{
    QJsonObject body = object(QStringLiteral("color_temperature"), object(QStringLiteral("mirek"), mirek));
    body.insert(QStringLiteral("on"), object(QStringLiteral("on"), true));
    lightCommand(id, body, object(QStringLiteral("color_temperature"), object(QStringLiteral("mirek_valid"), true)));
}

void HueClient::recallScene(const QString &sceneId)
{
    if (m_store->type(sceneId) != QLatin1String("scene"))
        return;
    m_queue->enqueue(QStringLiteral("scene"), sceneId,
                     object(QStringLiteral("recall"), object(QStringLiteral("action"), QStringLiteral("active"))));
}

QJsonArray HueClient::sceneActions(const QString &groupId) const
{
    // Each light as it is: off, or on with its brightness and either its colour temperature or
    // its colour, whichever it's showing.
    QJsonArray actions;
    for (const QString &lightId : m_store->lightsOf(groupId)) {
        if (!isFromBridge(lightId))
            continue; // scenes are the bridge's
        const QJsonObject light = m_store->resource(lightId);
        const bool on = HueJson::at(light, {QStringLiteral("on"), QStringLiteral("on")}).toBool();
        QJsonObject action = object(QStringLiteral("on"), object(QStringLiteral("on"), on));
        if (on) {
            const QJsonValue brightness = HueJson::at(light, {QStringLiteral("dimming"), QStringLiteral("brightness")});
            if (brightness.isDouble())
                action.insert(QStringLiteral("dimming"), object(QStringLiteral("brightness"), brightness));
            const QJsonObject ct = light.value(QStringLiteral("color_temperature")).toObject();
            const QJsonValue xy = HueJson::at(light, {QStringLiteral("color"), QStringLiteral("xy")});
            if (ct.value(QStringLiteral("mirek_valid")).toBool() && ct.value(QStringLiteral("mirek")).isDouble())
                action.insert(QStringLiteral("color_temperature"), object(QStringLiteral("mirek"), ct.value(QStringLiteral("mirek"))));
            else if (xy.isObject())
                action.insert(QStringLiteral("color"), object(QStringLiteral("xy"), xy));
        }
        QJsonObject target;
        target.insert(QStringLiteral("rid"), lightId);
        target.insert(QStringLiteral("rtype"), QStringLiteral("light"));
        QJsonObject entry;
        entry.insert(QStringLiteral("target"), target);
        entry.insert(QStringLiteral("action"), action);
        actions.append(entry);
    }
    return actions;
}

void HueClient::finishRequest(QNetworkReply *reply, const std::function<void(const QByteArray &)> &done)
{
    QTimer::singleShot(10 * 1000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const QString error = HueBridge::errorOf(reply, body);
        if (!error.isEmpty())
            setLastError(error);
        else if (done)
            done(body);
    });
}

void HueClient::createScene(const QString &groupId, const QString &name)
{
    const QString groupType = m_store->type(groupId);
    if (groupType != QLatin1String("room") && groupType != QLatin1String("zone"))
        return;
    QJsonObject group;
    group.insert(QStringLiteral("rid"), groupId);
    group.insert(QStringLiteral("rtype"), groupType);
    QJsonObject body;
    body.insert(QStringLiteral("type"), QStringLiteral("scene"));
    body.insert(QStringLiteral("metadata"), object(QStringLiteral("name"), name.trimmed().left(32)));
    body.insert(QStringLiteral("group"), group);
    body.insert(QStringLiteral("actions"), sceneActions(groupId));
    finishRequest(m_bridge->post(QStringLiteral("scene"), body), [this](const QByteArray &reply) {
        // The stream adds the scene itself.
        emit sceneCreated(HueBridge::createdId(reply));
    });
}

void HueClient::renameScene(const QString &sceneId, const QString &name)
{
    if (m_store->type(sceneId) == QLatin1String("scene"))
        command(sceneId, object(QStringLiteral("metadata"), object(QStringLiteral("name"), name.trimmed().left(32))));
}

void HueClient::storeScene(const QString &sceneId)
{
    if (m_store->type(sceneId) != QLatin1String("scene"))
        return;
    const QString groupId = HueJson::at(m_store->resource(sceneId), {QStringLiteral("group"), QStringLiteral("rid")}).toString();
    command(sceneId, object(QStringLiteral("actions"), sceneActions(groupId)));
}

void HueClient::deleteScene(const QString &sceneId)
{
    if (m_store->type(sceneId) != QLatin1String("scene"))
        return;
    finishRequest(m_bridge->remove(QStringLiteral("scene"), sceneId), [this, sceneId](const QByteArray &) {
        // Don't wait for the stream to say so.
        m_store->remove(sceneId);
    });
}

QJsonObject HueClient::zoneBody(const QString &name, const QString &archetype, const QStringList &lightIds) const
{
    QJsonArray children;
    QStringList seen;
    for (const QString &id : lightIds) {
        for (const QString &light : m_store->resolveLights(id)) {
            // The bridge only knows its own lights.
            if (seen.contains(light) || m_store->type(light) != QLatin1String("light")
                    || !HueResourceStore::sourceOf(light).isEmpty()) {
                continue;
            }
            seen.append(light);
            QJsonObject child;
            child.insert(QStringLiteral("rid"), light);
            child.insert(QStringLiteral("rtype"), QStringLiteral("light"));
            children.append(child);
        }
    }
    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), name.trimmed().left(32));
    metadata.insert(QStringLiteral("archetype"), archetype.isEmpty() ? QStringLiteral("other") : archetype);
    QJsonObject body;
    body.insert(QStringLiteral("metadata"), metadata);
    body.insert(QStringLiteral("children"), children);
    return body;
}

QStringList HueClient::otherLights() const
{
    QStringList lights;
    for (const QString &id : m_store->idsOfType(QStringLiteral("light"))) {
        if (!isFromBridge(id))
            lights.append(id);
    }
    return lights;
}

void HueClient::createZone(const QString &name, const QString &archetype, const QStringList &lightIds)
{
    QStringList others;
    for (const QString &id : lightIds) {
        for (const QString &light : m_store->resolveLights(id)) {
            if (!isFromBridge(light) && !others.contains(light))
                others.append(light);
        }
    }
    QJsonObject body = zoneBody(name, archetype, lightIds);
    if (body.value(QStringLiteral("children")).toArray().isEmpty()) {
        if (others.isEmpty() || name.trimmed().isEmpty())
            return;
        const QString zone = m_local->create(QStringLiteral("zone"), name, archetype);
        m_local->setMembers(zone, others, others);
        return;
    }
    body.insert(QStringLiteral("type"), QStringLiteral("zone"));
    // The stream adds the zone, and its grouped_light.
    finishRequest(m_bridge->post(QStringLiteral("zone"), body), [this, others](const QByteArray &reply) {
        const QString zone = HueBridge::createdId(reply);
        if (!zone.isEmpty() && !others.isEmpty())
            m_local->setMembers(zone, others, others);
    });
}

void HueClient::updateZone(const QString &zoneId, const QString &name, const QString &archetype,
                           const QStringList &lightIds)
{
    if (m_store->type(zoneId) != QLatin1String("zone"))
        return;
    QStringList others;
    for (const QString &id : lightIds) {
        for (const QString &light : m_store->resolveLights(id)) {
            if (!isFromBridge(light))
                others.append(light);
        }
    }
    m_local->setMembers(zoneId, otherLights(), others);
    if (!isFromBridge(zoneId)) {
        m_local->update(zoneId, name, archetype);
        return;
    }
    // The bridge wants at least one light in a zone; without, it keeps the ones it has.
    const QJsonObject body = zoneBody(name, archetype, lightIds);
    if (!body.value(QStringLiteral("children")).toArray().isEmpty())
        command(zoneId, body);
}

void HueClient::deleteZone(const QString &zoneId)
{
    if (m_store->type(zoneId) != QLatin1String("zone"))
        return;
    if (!isFromBridge(zoneId)) {
        m_local->remove(zoneId);
        return;
    }
    const QString groupedLight = m_store->groupedLightOf(zoneId);
    finishRequest(m_bridge->remove(QStringLiteral("zone"), zoneId), [this, zoneId, groupedLight](const QByteArray &) {
        m_local->remove(zoneId); // the Zigbee lights in it
        m_store->remove(zoneId);
        m_store->remove(groupedLight);
    });
}

void HueClient::renameLight(const QString &lightId, const QString &name)
{
    const QString trimmed = name.trimmed().left(32);
    if (m_store->type(lightId) != QLatin1String("light") || trimmed.isEmpty())
        return;
    const QJsonObject body = object(QStringLiteral("metadata"), object(QStringLiteral("name"), trimmed));
    const QString device = m_store->deviceOf(lightId);
    // A device with several lights keeps its own name; only this light changes.
    if (!device.isEmpty() && m_store->servicesOf(device, QStringLiteral("light")).size() == 1)
        command(device, body);
    command(lightId, body);
}

void HueClient::updateRoom(const QString &roomId, const QString &name, const QString &archetype)
{
    const QString trimmed = name.trimmed().left(32);
    if (HueResourceStore::sourceOf(roomId) == LocalGroups::Source) {
        if (!trimmed.isEmpty())
            m_local->update(roomId, trimmed, archetype);
        return;
    }
    if (m_store->type(roomId) != QLatin1String("room") || trimmed.isEmpty())
        return;
    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), trimmed);
    if (!archetype.isEmpty())
        metadata.insert(QStringLiteral("archetype"), archetype);
    command(roomId, object(QStringLiteral("metadata"), metadata));
}

QString HueClient::createRoom(const QString &name, const QString &archetype)
{
    if (name.trimmed().isEmpty())
        return QString();
    return m_local->create(QStringLiteral("room"), name, archetype);
}

void HueClient::deleteGroup(const QString &groupId)
{
    if (HueResourceStore::sourceOf(groupId) == LocalGroups::Source)
        m_local->remove(groupId);
}

QString HueClient::roomOfLight(const QString &lightId) const
{
    return m_store->roomsOfLight(lightId).value(0);
}

void HueClient::setLightRoom(const QString &lightId, const QString &roomId)
{
    if (isFromBridge(lightId) || m_store->type(lightId) != QLatin1String("light"))
        return; // the bridge's lights are in a room through their device, in the Hue app
    // Its zones stay; only the room changes.
    QStringList groups;
    for (const QString &group : m_local->groupsOf(lightId)) {
        if (m_store->type(group) == QLatin1String("zone"))
            groups.append(group);
    }
    if (m_store->type(roomId) == QLatin1String("room"))
        groups.append(roomId);
    m_local->setGroups(lightId, groups);
}

bool HueClient::isFromBridge(const QString &id) const
{
    return HueResourceStore::sourceOf(id).isEmpty();
}

QStringList HueClient::lightsOf(const QString &groupId) const
{
    return m_store->lightsOf(groupId);
}

QStringList HueClient::resolveLights(const QString &id) const
{
    return m_store->resolveLights(id);
}

void HueClient::setEnabled(const QString &id, bool enabled)
{
    command(id, object(QStringLiteral("enabled"), enabled));
}

void HueClient::setScheduleTime(const QString &scheduleId, int hour, int minute, const QStringList &days)
{
    QJsonObject schedule = m_store->resource(scheduleId);
    if (schedule.value(QStringLiteral("type")).toString() != QLatin1String("behavior_instance")
            || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return;
    }
    QJsonObject configuration = schedule.value(QStringLiteral("configuration")).toObject();
    const QString key = HueScheduleModel::whenKey(configuration);
    if (key.isEmpty())
        return;
    const QStringList path = HueScheduleModel::startPath(key);
    if (HueJson::at(configuration, path).toObject().value(QStringLiteral("type")).toString() != QLatin1String("time"))
        return; // sunrise or sunset: not a time to change
    QJsonObject time;
    time.insert(QStringLiteral("hour"), hour);
    time.insert(QStringLiteral("minute"), minute);
    QJsonObject timePoint;
    timePoint.insert(QStringLiteral("type"), QStringLiteral("time"));
    timePoint.insert(QStringLiteral("time"), time);

    QJsonObject when = configuration.value(key).toObject();
    if (key == QLatin1String("when_extended")) {
        QJsonObject start = when.value(QStringLiteral("start_at")).toObject();
        start.insert(QStringLiteral("time_point"), timePoint);
        when.insert(QStringLiteral("start_at"), start);
    } else {
        when.insert(QStringLiteral("time_point"), timePoint);
    }
    QJsonArray recurrence;
    for (const QString &day : HueScheduleModel::weekDays()) {
        if (days.contains(day))
            recurrence.append(day);
    }
    if (recurrence.isEmpty())
        when.remove(QStringLiteral("recurrence_days"));
    else
        when.insert(QStringLiteral("recurrence_days"), recurrence);
    configuration.insert(key, when);

    // The whole configuration, as the bridge checks it against the script's schema. In the
    // store as a whole too: merging wouldn't drop recurrence_days.
    schedule.insert(QStringLiteral("configuration"), configuration);
    m_store->add(schedule);
    m_queue->enqueue(QStringLiteral("behavior_instance"), scheduleId,
                     object(QStringLiteral("configuration"), configuration));
}

void HueClient::deleteSchedule(const QString &scheduleId)
{
    if (m_store->type(scheduleId) != QLatin1String("behavior_instance"))
        return;
    finishRequest(m_bridge->remove(QStringLiteral("behavior_instance"), scheduleId), [this, scheduleId](const QByteArray &) {
        m_store->remove(scheduleId);
    });
}

void HueClient::allOff()
{
    for (const QString &id : m_store->idsOfType(QStringLiteral("light"))) {
        if (!HueResourceStore::sourceOf(id).isEmpty()
                && HueJson::at(m_store->resource(id), {QStringLiteral("on"), QStringLiteral("on")}).toBool()) {
            setOn(id, false);
        }
    }

    // bridge_home has a grouped_light covering every light.
    const QStringList homes = m_store->idsOfType(QStringLiteral("bridge_home"));
    const QString all = homes.isEmpty() ? QString() : m_store->groupedLightOf(homes.first());
    if (!all.isEmpty()) {
        setOn(all, false);
        // Its own state doesn't tell the rooms' grouped_lights; the stream will, but late.
        for (const QString &id : m_store->idsOfType(QStringLiteral("grouped_light"))) {
            QJsonObject off = object(QStringLiteral("on"), object(QStringLiteral("on"), false));
            off.insert(QStringLiteral("id"), id);
            m_store->applyUpdate(off);
        }
        return;
    }
    for (const QString &room : m_store->idsOfType(QStringLiteral("room"))) {
        const QString groupedLight = m_store->groupedLightOf(room);
        if (!groupedLight.isEmpty())
            setOn(groupedLight, false);
    }
}

void HueClient::toggleGroup(const QString &groupId)
{
    if (m_store->lightsOf(groupId).isEmpty())
        return;
    setOn(groupId, !isGroupOn(groupId));
}

bool HueClient::isGroupOn(const QString &groupId) const
{
    const QString groupedLight = m_store->groupedLightOf(groupId);
    if (HueJson::at(m_store->resource(groupedLight), {QStringLiteral("on"), QStringLiteral("on")}).toBool())
        return true;
    for (const QString &light : m_store->lightsOf(groupId)) {
        if ((groupedLight.isEmpty() || !isFromBridge(light))
                && HueJson::at(m_store->resource(light), {QStringLiteral("on"), QStringLiteral("on")}).toBool()) {
            return true;
        }
    }
    return false;
}

QVariantList HueClient::coverActions() const
{
    QVariantList actions;
    for (int slot = 0; slot < 2; ++slot) {
        const QString prefix = QStringLiteral("cover/action%1").arg(slot + 1);
        // Out of the box, the first button switches everything off, as the cover always did.
        QString kind = m_settings->value(prefix + QStringLiteral("Kind"),
                                         slot == 0 ? QStringLiteral("alloff") : QStringLiteral("none")).toString();
        const QString id = m_settings->value(prefix + QStringLiteral("Id")).toString();
        QVariantMap action;
        if (kind == QLatin1String("group") || kind == QLatin1String("scene")) {
            // Gone from the bridge (or not loaded yet): no button.
            if (m_store->type(id).isEmpty() || (kind == QLatin1String("scene")) != (m_store->type(id) == QLatin1String("scene")))
                kind = QStringLiteral("none");
            action.insert(QStringLiteral("id"), id);
            action.insert(QStringLiteral("name"), m_store->name(id));
            action.insert(QStringLiteral("archetype"), HueJson::at(m_store->resource(id),
                          {QStringLiteral("metadata"), QStringLiteral("archetype")}).toString());
            if (kind == QLatin1String("scene")) {
                const QString group = HueJson::at(m_store->resource(id), {QStringLiteral("group"), QStringLiteral("rid")}).toString();
                action.insert(QStringLiteral("groupName"), m_store->name(group));
            }
        }
        action.insert(QStringLiteral("kind"), kind);
        actions.append(action);
    }
    return actions;
}

void HueClient::setCoverAction(int slot, const QString &kind, const QString &id)
{
    if (slot < 0 || slot > 1)
        return;
    const QString prefix = QStringLiteral("cover/action%1").arg(slot + 1);
    m_settings->setValue(prefix + QStringLiteral("Kind"), kind);
    m_settings->setValue(prefix + QStringLiteral("Id"), id);
    m_settings->sync();
    emit coverActionsChanged();
}

void HueClient::runCoverAction(int slot)
{
    const QVariantMap action = coverActions().value(slot).toMap();
    const QString kind = action.value(QStringLiteral("kind")).toString();
    const QString id = action.value(QStringLiteral("id")).toString();
    if (kind == QLatin1String("alloff"))
        allOff();
    else if (kind == QLatin1String("group"))
        toggleGroup(id);
    else if (kind == QLatin1String("scene"))
        recallScene(id);
}

bool HueClient::isMqttConfigured() const
{
    return !mqttHost().isEmpty();
}

QString HueClient::mqttHost() const
{
    return m_settings->value(kMqttHost).toString();
}

int HueClient::mqttPort() const
{
    return m_settings->value(kMqttPort, 1883).toInt();
}

QString HueClient::mqttUser() const
{
    return m_settings->value(kMqttUser).toString();
}

QString HueClient::mqttBaseTopic() const
{
    return m_settings->value(kMqttBase, QStringLiteral("zigbee2mqtt")).toString();
}

HueClient::MqttState HueClient::mqttState() const
{
    if (!isMqttConfigured())
        return MqttOff;
    switch (m_mqtt->state()) {
    case MqttClient::Idle:
    case MqttClient::Connecting: return MqttConnecting;
    case MqttClient::Connected: return MqttConnected;
    case MqttClient::WaitingToReconnect: return MqttReconnecting;
    case MqttClient::Refused: return MqttRefused;
    }
    return MqttOff;
}

void HueClient::setMqttConfig(const QString &host, int port, const QString &user,
                              const QString &password, const QString &baseTopic)
{
    const QString trimmedUser = user.trimmed();
    if (!password.isEmpty() || trimmedUser != mqttUser())
        m_settings->setValue(kMqttPassword, password);
    m_settings->setValue(kMqttHost, host.trimmed());
    m_settings->setValue(kMqttPort, port > 0 && port < 65536 ? port : 1883);
    m_settings->setValue(kMqttUser, trimmedUser);
    m_settings->setValue(kMqttBase, baseTopic.trimmed().isEmpty() ? QStringLiteral("zigbee2mqtt")
                                                                  : baseTopic.trimmed());
    m_settings->sync();
    applyMqttConfig();
    emit mqttConfigChanged();
}

void HueClient::forgetMqtt()
{
    m_settings->remove(QStringLiteral("mqtt"));
    m_settings->sync();
    applyMqttConfig();
    emit mqttConfigChanged();
}

void HueClient::applyMqttConfig()
{
    m_mqtt->stop();
    m_mqtt->clearSubscriptions();
    m_zigbee->clear();
    m_mqttError.clear();
    MqttConfig config;
    config.host = mqttHost();
    config.port = mqttPort();
    config.user = mqttUser();
    config.password = m_settings->value(kMqttPassword).toString();
    m_mqtt->setConfig(config);
    m_zigbee->setBaseTopic(mqttBaseTopic());
    if (config.isValid())
        m_mqtt->start();
    emit mqttStateChanged();
}

QColor HueClient::mirekColor(int mirek) const
{
    return HueColor::mirekToRgb(mirek);
}

void HueClient::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void HueClient::setPairing(PairingState state, int secondsLeft, const QString &error)
{
    m_pairingState = state;
    m_pairingSecondsLeft = secondsLeft;
    m_pairingError = error;
    emit pairingChanged();
}
