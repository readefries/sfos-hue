// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huemodels.h"

#include "huecolor.h"
#include "huejson.h"

#include <QJsonArray>

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

QStringList HueRoomModel::dependenciesOf(const QString &id) const
{
    return QStringList() << id << m_store->groupedLightOf(id);
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
    case OnRole: return at(groupedLight, "on", "on").toBool();
    case BrightnessRole: return at(groupedLight, "dimming", "brightness").toDouble();
    case LightCountRole: return m_store->lightsOf(id).size();
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
    if (!m_lightId.isEmpty())
        return m_store->contains(m_lightId) ? QStringList(m_lightId) : QStringList();
    return sortedByName(m_groupId.isEmpty() ? m_store->idsOfType(QStringLiteral("light"))
                                            : m_store->lightsOf(m_groupId));
}

QStringList HueLightModel::dependenciesOf(const QString &id) const
{
    return QStringList() << id << m_store->connectivityOfLight(id);
}

QVariant HueLightModel::dataFor(const QString &id, int role) const
{
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
