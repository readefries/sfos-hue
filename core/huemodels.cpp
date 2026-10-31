// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huemodels.h"

#include "huecolor.h"
#include "huejson.h"

#include <QDateTime>
#include <QJsonArray>
#include <QtMath>

namespace {

QJsonValue at(const QJsonObject &obj, const char *a, const char *b = nullptr, const char *c = nullptr)
{
    QStringList path(QString::fromLatin1(a));
    if (b)
        path.append(QString::fromLatin1(b));
    if (c)
        path.append(QString::fromLatin1(c));
    return HueJson::at(obj, path);
}

QColor colorOfXy(const QJsonValue &xy)
{
    const QJsonObject obj = xy.toObject();
    return HueColor::xyToRgb(QPointF(obj.value(QStringLiteral("x")).toDouble(),
                                     obj.value(QStringLiteral("y")).toDouble()));
}

// Warm white, for lamps that only dim.
const int kDefaultMirek = 366;

} // namespace

// Rooms

HueRoomModel::HueRoomModel(QObject *parent)
    : HueListModel(parent)
{
}

QHash<int, QByteArray> HueRoomModel::roleNames() const
{
    return {
        {IdRole, "resourceId"},
        {KindRole, "kind"},
        {NameRole, "name"},
        {ArchetypeRole, "archetype"},
        {GroupedLightIdRole, "groupedLightId"},
        {OnRole, "on"},
        {BrightnessRole, "brightness"},
        {LightCountRole, "lightCount"},
        {DimmableRole, "dimmable"},
    };
}

void HueRoomModel::setGroupId(const QString &groupId)
{
    if (m_groupId == groupId)
        return;
    m_groupId = groupId;
    emit groupIdChanged();
    refresh();
}

QStringList HueRoomModel::collectIds() const
{
    if (!m_groupId.isEmpty())
        return m_store->contains(m_groupId) ? QStringList(m_groupId) : QStringList();
    return sortedByName(m_store->idsOfType(QStringLiteral("room")))
            + sortedByName(m_store->idsOfType(QStringLiteral("zone")));
}

namespace {

// Whether the room's state is its grouped_light's, or has to come from its lights: when it has
// no grouped_light (Lampje's own rooms) or lights the bridge doesn't know (Zigbee ones).
bool stateFromLights(const HueResourceStore *store, const QString &groupId)
{
    if (store->groupedLightOf(groupId).isEmpty())
        return true;
    for (const QString &light : store->lightsOf(groupId)) {
        if (!HueResourceStore::sourceOf(light).isEmpty())
            return true;
    }
    return false;
}

} // namespace

QStringList HueRoomModel::dependenciesOf(const QString &id) const
{
    QStringList dependencies;
    dependencies << id << m_store->groupedLightOf(id);
    if (stateFromLights(m_store, id))
        dependencies += m_store->lightsOf(id);
    return dependencies;
}

QVariant HueRoomModel::dataFor(const QString &id, int role) const
{
    const QJsonObject group = m_store->resource(id);
    const QString groupedLightId = m_store->groupedLightOf(id);
    const QJsonObject groupedLight = m_store->resource(groupedLightId);
    switch (role) {
    case IdRole: return id;
    case KindRole: return group.value(QStringLiteral("type")).toString();
    case NameRole: return at(group, "metadata", "name").toString();
    case ArchetypeRole: return at(group, "metadata", "archetype").toString();
    case GroupedLightIdRole: return groupedLightId;
    case OnRole:
    case BrightnessRole:
        if (!stateFromLights(m_store, id)) {
            return role == OnRole ? QVariant(at(groupedLight, "on", "on").toBool())
                                  : QVariant(at(groupedLight, "dimming", "brightness").toDouble());
        } else {
            // Like the bridge: on if any light is, brightness of the lights that are on. The
            // bridge's lights count as its grouped_light says, if there is one.
            double sum = 0;
            int on = 0;
            int bridgeLights = 0;
            for (const QString &lightId : m_store->lightsOf(id)) {
                if (!groupedLightId.isEmpty() && HueResourceStore::sourceOf(lightId).isEmpty()) {
                    ++bridgeLights;
                    continue;
                }
                const QJsonObject light = m_store->resource(lightId);
                if (!at(light, "on", "on").toBool())
                    continue;
                const QJsonValue brightness = at(light, "dimming", "brightness");
                sum += brightness.isDouble() ? brightness.toDouble() : 100.0;
                ++on;
            }
            if (bridgeLights > 0 && at(groupedLight, "on", "on").toBool()) {
                sum += bridgeLights * at(groupedLight, "dimming", "brightness").toDouble();
                on += bridgeLights;
            }
            return role == OnRole ? QVariant(on > 0) : QVariant(on > 0 ? sum / on : 0.0);
        }
    case LightCountRole: return m_store->lightsOf(id).size();
    case DimmableRole:
        for (const QString &lightId : m_store->lightsOf(id)) {
            if (m_store->resource(lightId).value(QStringLiteral("dimming")).isObject())
                return true;
        }
        return false;
    }
    return QVariant();
}

// Lights

HueLightModel::HueLightModel(QObject *parent)
    : HueListModel(parent)
{
}

QHash<int, QByteArray> HueLightModel::roleNames() const
{
    return {
        {IdRole, "resourceId"},
        {NameRole, "name"},
        {ArchetypeRole, "archetype"},
        {OnRole, "on"},
        {BrightnessRole, "brightness"},
        {ReachableRole, "reachable"},
        {SupportsDimmingRole, "supportsDimming"},
        {SupportsColorRole, "supportsColor"},
        {SupportsTemperatureRole, "supportsTemperature"},
        {MirekRole, "mirek"},
        {MirekMinRole, "mirekMin"},
        {MirekMaxRole, "mirekMax"},
        {DisplayColorRole, "displayColor"},
        {LightCountRole, "lightCount"},
    };
}

void HueLightModel::setGroupId(const QString &groupId)
{
    if (m_groupId == groupId)
        return;
    m_groupId = groupId;
    emit groupIdChanged();
    refresh();
}

void HueLightModel::setLightId(const QString &lightId)
{
    if (m_lightId == lightId)
        return;
    m_lightId = lightId;
    emit lightIdChanged();
    refresh();
}

void HueLightModel::setWithoutRoom(bool withoutRoom)
{
    if (m_withoutRoom == withoutRoom)
        return;
    m_withoutRoom = withoutRoom;
    emit withoutRoomChanged();
    refresh();
}

void HueLightModel::setGroupFixtures(bool groupFixtures)
{
    if (m_groupFixtures == groupFixtures)
        return;
    m_groupFixtures = groupFixtures;
    emit groupFixturesChanged();
    refresh();
}

QColor HueLightModel::displayColor(const QJsonObject &light)
{
    const QJsonValue ct = light.value(QStringLiteral("color_temperature"));
    // mirek_valid is false while the lamp shows an xy colour instead.
    if (ct.isObject() && at(light, "color_temperature", "mirek_valid").toBool())
        return HueColor::mirekToRgb(at(light, "color_temperature", "mirek").toInt());
    if (light.value(QStringLiteral("color")).isObject())
        return colorOfXy(at(light, "color", "xy"));
    return HueColor::mirekToRgb(kDefaultMirek);
}

QStringList HueLightModel::collectIds() const
{
    if (!m_lightId.isEmpty()) {
        const QStringList lights = m_store->resolveLights(m_lightId);
        if (!HueResourceStore::isFixtureId(m_lightId))
            return m_store->contains(m_lightId) ? lights : QStringList();
        if (lights.isEmpty())
            return QStringList();
        return m_groupFixtures ? QStringList(m_lightId) : sortedByName(lights);
    }
    QStringList lights = m_groupId.isEmpty() ? m_store->idsOfType(QStringLiteral("light"))
                                             : m_store->lightsOf(m_groupId);
    if (m_withoutRoom) {
        QStringList placed;
        for (const QString &room : m_store->idsOfType(QStringLiteral("room")))
            placed += m_store->lightsOf(room);
        QStringList unplaced;
        for (const QString &light : lights) {
            if (!placed.contains(light))
                unplaced.append(light);
        }
        lights = unplaced;
    }
    return sortedByName(m_groupFixtures ? m_store->withFixtures(lights, m_groupId) : lights);
}

QStringList HueLightModel::dependenciesOf(const QString &id) const
{
    QStringList dependencies;
    for (const QString &light : m_store->resolveLights(id))
        dependencies << light << m_store->connectivityOfLight(light);
    return dependencies;
}

QVariant HueLightModel::fixtureData(const QString &id, int role) const
{
    const QStringList lights = m_store->resolveLights(id);
    switch (role) {
    case IdRole: return id;
    case NameRole: return m_store->name(id);
    case LightCountRole: return lights.size();
    case BrightnessRole: {
        // Of the spots that are on, like the bridge does for rooms.
        double sum = 0, sumOn = 0;
        int on = 0;
        for (const QString &light : lights) {
            const double brightness = dataFor(light, BrightnessRole).toDouble();
            sum += brightness;
            if (dataFor(light, OnRole).toBool()) {
                sumOn += brightness;
                ++on;
            }
        }
        return on > 0 ? sumOn / on : sum / lights.size();
    }
    case OnRole:
    case SupportsDimmingRole:
    case SupportsColorRole:
    case SupportsTemperatureRole:
        for (const QString &light : lights) {
            if (dataFor(light, role).toBool())
                return true;
        }
        return false;
    case ReachableRole:
        for (const QString &light : lights) {
            if (!dataFor(light, role).toBool())
                return false;
        }
        return true;
    case DisplayColorRole:
        for (const QString &light : lights) {
            if (dataFor(light, OnRole).toBool())
                return dataFor(light, role);
        }
        return dataFor(lights.first(), role);
    case MirekRole:
    case MirekMinRole:
    case MirekMaxRole:
        for (const QString &light : lights) {
            if (dataFor(light, SupportsTemperatureRole).toBool())
                return dataFor(light, role);
        }
        return dataFor(lights.first(), role);
    }
    return dataFor(lights.first(), role);
}

QVariant HueLightModel::dataFor(const QString &id, int role) const
{
    if (HueResourceStore::isFixtureId(id)) {
        const QStringList lights = m_store->resolveLights(id);
        return lights.isEmpty() ? QVariant() : fixtureData(id, role);
    }
    const QJsonObject light = m_store->resource(id);
    switch (role) {
    case IdRole: return id;
    case NameRole: return at(light, "metadata", "name").toString();
    case ArchetypeRole: return at(light, "metadata", "archetype").toString();
    case OnRole: return at(light, "on", "on").toBool();
    case BrightnessRole: return at(light, "dimming", "brightness").toDouble();
    case ReachableRole: return m_store->isReachable(id);
    case SupportsDimmingRole: return light.value(QStringLiteral("dimming")).isObject();
    case SupportsColorRole: return light.value(QStringLiteral("color")).isObject();
    case SupportsTemperatureRole: return light.value(QStringLiteral("color_temperature")).isObject();
    case MirekRole: {
        const QJsonValue mirek = at(light, "color_temperature", "mirek");
        return mirek.isDouble() ? mirek.toInt() : kDefaultMirek;
    }
    case MirekMinRole: return at(light, "color_temperature", "mirek_schema", "mirek_minimum").toInt(153);
    case MirekMaxRole: return at(light, "color_temperature", "mirek_schema", "mirek_maximum").toInt(500);
    case DisplayColorRole: return displayColor(light);
    case LightCountRole: return 1;
    }
    return QVariant();
}

// Scenes

HueSceneModel::HueSceneModel(QObject *parent)
    : HueListModel(parent)
{
}

QHash<int, QByteArray> HueSceneModel::roleNames() const
{
    return {
        {IdRole, "resourceId"},
        {NameRole, "name"},
        {ActiveRole, "active"},
        {ColorRole, "sceneColor"},
    };
}

void HueSceneModel::setGroupId(const QString &groupId)
{
    if (m_groupId == groupId)
        return;
    m_groupId = groupId;
    emit groupIdChanged();
    refresh();
}

QStringList HueSceneModel::collectIds() const
{
    return m_groupId.isEmpty() ? QStringList() : sortedByName(m_store->scenesOf(m_groupId));
}

QVariant HueSceneModel::dataFor(const QString &id, int role) const
{
    const QJsonObject scene = m_store->resource(id);
    switch (role) {
    case IdRole: return id;
    case NameRole: return at(scene, "metadata", "name").toString();
    case ActiveRole: {
        const QString active = at(scene, "status", "active").toString();
        return !active.isEmpty() && active != QLatin1String("inactive");
    }
    case ColorRole: {
        // The palette if it has one, otherwise the first light's colour.
        const QJsonArray palette = at(scene, "palette", "color").toArray();
        if (!palette.isEmpty())
            return colorOfXy(at(palette.at(0).toObject(), "color", "xy"));
        const QJsonArray actions = scene.value(QStringLiteral("actions")).toArray();
        for (const QJsonValue &value : actions) {
            const QJsonObject action = value.toObject().value(QStringLiteral("action")).toObject();
            if (action.value(QStringLiteral("color")).isObject())
                return colorOfXy(at(action, "color", "xy"));
            if (action.value(QStringLiteral("color_temperature")).isObject())
                return HueColor::mirekToRgb(at(action, "color_temperature", "mirek").toInt(kDefaultMirek));
        }
        return HueColor::mirekToRgb(kDefaultMirek);
    }
    }
    return QVariant();
}

// Sensors

namespace {

const QStringList kSensorServices = {
    QStringLiteral("motion"), QStringLiteral("temperature"), QStringLiteral("light_level"),
    QStringLiteral("button"), QStringLiteral("relative_rotary"), QStringLiteral("contact"),
    QStringLiteral("device_power"),
};

QDateTime timestamp(const QJsonValue &value)
{
    QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODate);
    time.setTimeSpec(Qt::UTC);
    return time.isValid() ? time.toLocalTime() : QDateTime();
}

} // namespace

HueSensorModel::HueSensorModel(QObject *parent)
    : HueListModel(parent)
{
}

QHash<int, QByteArray> HueSensorModel::roleNames() const
{
    return {
        {IdRole, "resourceId"},
        {NameRole, "name"},
        {ProductNameRole, "productName"},
        {KindRole, "kind"},
        {ReachableRole, "reachable"},
        {MotionIdRole, "motionId"},
        {MotionEnabledRole, "motionEnabled"},
        {MotionRole, "motion"},
        {MotionChangedRole, "motionChanged"},
        {TemperatureRole, "temperature"},
        {LightLevelRole, "lightLevel"},
        {BatteryLevelRole, "batteryLevel"},
        {BatteryLowRole, "batteryLow"},
        {LastButtonRole, "lastButton"},
        {LastButtonEventRole, "lastButtonEvent"},
        {LastButtonTimeRole, "lastButtonTime"},
    };
}

double HueSensorModel::lux(int lightLevel)
{
    return qPow(10.0, (lightLevel - 1) / 10000.0);
}

QStringList HueSensorModel::collectIds() const
{
    QStringList devices;
    for (const QString &id : m_store->idsOfType(QStringLiteral("device"))) {
        // Lamps can have device_power too (none do yet); this is about sensors.
        if (!m_store->servicesOf(id, QStringLiteral("light")).isEmpty())
            continue;
        for (const QString &type : kSensorServices) {
            if (!m_store->servicesOf(id, type).isEmpty()) {
                devices.append(id);
                break;
            }
        }
    }
    return sortedByName(devices);
}

QStringList HueSensorModel::dependenciesOf(const QString &id) const
{
    QStringList dependencies = HueJson::refs(m_store->resource(id), QStringLiteral("services"),
                                             QStringLiteral("zigbee_connectivity"));
    dependencies.append(id);
    for (const QString &type : kSensorServices)
        dependencies += m_store->servicesOf(id, type);
    return dependencies;
}

QJsonObject HueSensorModel::service(const QString &deviceId, const QString &rtype) const
{
    const QStringList ids = m_store->servicesOf(deviceId, rtype);
    return ids.isEmpty() ? QJsonObject() : m_store->resource(ids.first());
}

QJsonObject HueSensorModel::lastButton(const QString &deviceId) const
{
    QJsonObject last;
    QDateTime lastTime;
    for (const QString &id : m_store->servicesOf(deviceId, QStringLiteral("button"))) {
        const QJsonObject button = m_store->resource(id);
        const QDateTime time = timestamp(at(button, "button", "button_report", "updated"));
        if (time.isValid() && (!lastTime.isValid() || time > lastTime)) {
            last = button;
            lastTime = time;
        }
    }
    return last;
}

QVariant HueSensorModel::dataFor(const QString &id, int role) const
{
    const QJsonObject device = m_store->resource(id);
    switch (role) {
    case IdRole: return id;
    case NameRole: return at(device, "metadata", "name").toString();
    case ProductNameRole: return at(device, "product_data", "product_name").toString();
    case KindRole:
        if (!m_store->servicesOf(id, QStringLiteral("motion")).isEmpty())
            return QStringLiteral("motion");
        if (!m_store->servicesOf(id, QStringLiteral("button")).isEmpty())
            return QStringLiteral("switch");
        if (!m_store->servicesOf(id, QStringLiteral("contact")).isEmpty())
            return QStringLiteral("contact");
        return QStringLiteral("sensor");
    case ReachableRole: return m_store->isDeviceReachable(id);
    case MotionIdRole: return m_store->servicesOf(id, QStringLiteral("motion")).value(0);
    case MotionEnabledRole: return service(id, QStringLiteral("motion")).value(QStringLiteral("enabled")).toBool();
    case MotionRole: return at(service(id, QStringLiteral("motion")), "motion", "motion").toBool();
    case MotionChangedRole:
        return timestamp(at(service(id, QStringLiteral("motion")), "motion", "motion_report", "changed"));
    case TemperatureRole: {
        const QJsonObject temperature = service(id, QStringLiteral("temperature"));
        if (!at(temperature, "temperature", "temperature_valid").toBool())
            return QVariant();
        return at(temperature, "temperature", "temperature").toDouble();
    }
    case LightLevelRole: {
        const QJsonObject light = service(id, QStringLiteral("light_level"));
        if (!at(light, "light", "light_level_valid").toBool())
            return QVariant();
        return lux(at(light, "light", "light_level").toInt());
    }
    case BatteryLevelRole: {
        const QJsonValue level = at(service(id, QStringLiteral("device_power")), "power_state", "battery_level");
        return level.isDouble() ? level.toInt() : -1;
    }
    case BatteryLowRole: {
        const QString state = at(service(id, QStringLiteral("device_power")), "power_state", "battery_state").toString();
        return state == QLatin1String("low") || state == QLatin1String("critical");
    }
    case LastButtonRole: return at(lastButton(id), "metadata", "control_id").toInt();
    case LastButtonEventRole: return at(lastButton(id), "button", "button_report", "event").toString();
    case LastButtonTimeRole: return timestamp(at(lastButton(id), "button", "button_report", "updated"));
    }
    return QVariant();
}
