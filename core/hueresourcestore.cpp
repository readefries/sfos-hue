// SPDX-License-Identifier: AGPL-3.0-or-later
#include "hueresourcestore.h"

#include "huejson.h"

#include <QRegularExpression>

namespace {
const QString kId = QStringLiteral("id");
const QString kType = QStringLiteral("type");
}

HueResourceStore::HueResourceStore(QObject *parent)
    : QObject(parent)
{
}

void HueResourceStore::reset(const QJsonArray &resources)
{
    m_resources.clear();
    for (const QJsonValue &value : resources) {
        const QJsonObject obj = value.toObject();
        const QString id = obj.value(kId).toString();
        if (!id.isEmpty())
            m_resources.insert(id, obj);
    }
    emit resourcesReset();
}

void HueResourceStore::clear()
{
    m_resources.clear();
    emit resourcesReset();
}

void HueResourceStore::applyEvents(const QJsonArray &events)
{
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        const QString kind = event.value(kType).toString();
        const QJsonArray data = event.value(QStringLiteral("data")).toArray();
        for (const QJsonValue &item : data) {
            const QJsonObject obj = item.toObject();
            if (kind == QLatin1String("update"))
                applyUpdate(obj);
            else if (kind == QLatin1String("add"))
                add(obj);
            else if (kind == QLatin1String("delete"))
                remove(obj.value(kId).toString());
        }
    }
}

void HueResourceStore::applyUpdate(const QJsonObject &partial)
{
    const QString id = partial.value(kId).toString();
    auto it = m_resources.find(id);
    if (it == m_resources.end())
        return; // not loaded (yet); the next full load has it
    *it = HueJson::merged(*it, partial);
    emit resourceChanged(id, it->value(kType).toString());
}

void HueResourceStore::add(const QJsonObject &resource)
{
    const QString id = resource.value(kId).toString();
    if (id.isEmpty())
        return;
    const bool existed = m_resources.contains(id);
    m_resources.insert(id, resource);
    const QString type = resource.value(kType).toString();
    if (existed)
        emit resourceChanged(id, type);
    else
        emit resourceAdded(id, type);
}

void HueResourceStore::remove(const QString &id)
{
    auto it = m_resources.find(id);
    if (it == m_resources.end())
        return;
    const QString type = it->value(kType).toString();
    m_resources.erase(it);
    emit resourceRemoved(id, type);
}

QString HueResourceStore::type(const QString &id) const
{
    return m_resources.value(id).value(kType).toString();
}

QString HueResourceStore::name(const QString &id) const
{
    if (isFixtureId(id)) {
        // What the lights' names have in common ("Hue Runner spot" for "Hue Runner spot 1", ...),
        // or the product's name if they were named apart.
        const QStringList lights = lightsOfFixture(id);
        QString common = lights.isEmpty() ? QString() : name(lights.first());
        for (const QString &light : lights) {
            const QString other = name(light);
            int n = 0;
            while (n < common.size() && n < other.size() && common.at(n) == other.at(n))
                ++n;
            common.truncate(n);
        }
        common = common.remove(QRegularExpression(QStringLiteral("[\\s\\d#.,:_-]+$")));
        if (common.size() >= 3)
            return common;
        const QJsonObject owner = m_resources.value(lights.value(0)).value(QStringLiteral("owner")).toObject();
        return HueJson::at(m_resources.value(owner.value(QStringLiteral("rid")).toString()),
                           {QStringLiteral("product_data"), QStringLiteral("product_name")}).toString();
    }
    return HueJson::at(m_resources.value(id), {QStringLiteral("metadata"), QStringLiteral("name")}).toString();
}

QStringList HueResourceStore::idsOfType(const QString &type) const
{
    QStringList ids;
    for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
        if (it->value(kType).toString() == type)
            ids.append(it.key());
    }
    return ids;
}

QString HueResourceStore::groupedLightOf(const QString &groupId) const
{
    return HueJson::firstRef(m_resources.value(groupId), QStringLiteral("services"),
                             QStringLiteral("grouped_light"));
}

QString HueResourceStore::groupOfGroupedLight(const QString &groupedLightId) const
{
    const QJsonObject owner = m_resources.value(groupedLightId).value(QStringLiteral("owner")).toObject();
    return owner.value(QStringLiteral("rid")).toString();
}

QStringList HueResourceStore::lightsOf(const QString &groupId) const
{
    const QJsonObject group = m_resources.value(groupId);
    const QString children = QStringLiteral("children");
    QStringList lights = HueJson::refs(group, children, QStringLiteral("light"));
    for (const QString &deviceId : HueJson::refs(group, children, QStringLiteral("device"))) {
        for (const QString &lightId : HueJson::refs(m_resources.value(deviceId), QStringLiteral("services"),
                                                    QStringLiteral("light"))) {
            if (!lights.contains(lightId))
                lights.append(lightId);
        }
    }
    // Only what's actually loaded, so callers never see dangling ids.
    QStringList known;
    for (const QString &id : lights) {
        if (m_resources.contains(id))
            known.append(id);
    }
    return known;
}

QString HueResourceStore::connectivityOfLight(const QString &lightId) const
{
    const QJsonObject owner = m_resources.value(lightId).value(QStringLiteral("owner")).toObject();
    const QJsonObject device = m_resources.value(owner.value(QStringLiteral("rid")).toString());
    return HueJson::firstRef(device, QStringLiteral("services"), QStringLiteral("zigbee_connectivity"));
}

bool HueResourceStore::isReachable(const QString &lightId) const
{
    const QString connectivity = connectivityOfLight(lightId);
    if (connectivity.isEmpty() || !m_resources.contains(connectivity))
        return true;
    const QString status = m_resources.value(connectivity).value(QStringLiteral("status")).toString();
    return status.isEmpty() || status == QLatin1String("connected");
}

QStringList HueResourceStore::scenesOf(const QString &groupId) const
{
    QStringList scenes;
    for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
        if (it->value(kType).toString() != QLatin1String("scene"))
            continue;
        const QJsonObject group = it->value(QStringLiteral("group")).toObject();
        if (group.value(QStringLiteral("rid")).toString() == groupId)
            scenes.append(it.key());
    }
    return scenes;
}

bool HueResourceStore::isFixtureId(const QString &id)
{
    return id.startsWith(QLatin1String("fixture:"));
}

QString HueResourceStore::fixtureId(const QString &productNumber, const QString &groupId)
{
    return QStringLiteral("fixture:%1:%2").arg(productNumber, groupId);
}

QString HueResourceStore::fixtureProductOf(const QString &lightId) const
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{12})_\\d{2}$"));
    const QJsonObject owner = m_resources.value(lightId).value(QStringLiteral("owner")).toObject();
    const QString model = HueJson::at(m_resources.value(owner.value(QStringLiteral("rid")).toString()),
                                      {QStringLiteral("product_data"), QStringLiteral("model_id")}).toString();
    const QRegularExpressionMatch match = pattern.match(model);
    return match.hasMatch() ? match.captured(1) : QString();
}

QStringList HueResourceStore::lightsOfFixture(const QString &fixtureId) const
{
    const QStringList parts = fixtureId.split(QLatin1Char(':'));
    if (parts.size() != 3 || parts.at(1).isEmpty())
        return QStringList();
    const QString groupId = parts.at(2);
    const QStringList candidates = groupId.isEmpty() ? idsOfType(QStringLiteral("light")) : lightsOf(groupId);
    QStringList lights;
    for (const QString &light : candidates) {
        if (fixtureProductOf(light) == parts.at(1))
            lights.append(light);
    }
    lights.sort();
    return lights;
}

QStringList HueResourceStore::withFixtures(const QStringList &lights, const QString &groupId) const
{
    QHash<QString, int> members;
    for (const QString &light : lights) {
        const QString product = fixtureProductOf(light);
        if (!product.isEmpty())
            ++members[product];
    }
    QStringList result;
    for (const QString &light : lights) {
        const QString product = fixtureProductOf(light);
        if (product.isEmpty() || members.value(product) < 2) {
            result.append(light);
        } else {
            const QString fixture = fixtureId(product, groupId);
            if (!result.contains(fixture))
                result.append(fixture);
        }
    }
    return result;
}

QStringList HueResourceStore::resolveLights(const QString &id) const
{
    return isFixtureId(id) ? lightsOfFixture(id) : QStringList(id);
}
