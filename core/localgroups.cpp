// SPDX-License-Identifier: AGPL-3.0-or-later
#include "localgroups.h"

#include "hueresourcestore.h"

#include <QJsonDocument>
#include <QSettings>
#include <QUuid>

namespace {
const QString kGroups = QStringLiteral("local/groups");
const QString kPlacement = QStringLiteral("local/placement");
}

const QString LocalGroups::Source = QStringLiteral("local");

LocalGroups::LocalGroups(QSettings *settings, HueResourceStore *store, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
{
}

QJsonArray LocalGroups::groups() const
{
    return QJsonDocument::fromJson(m_settings->value(kGroups).toByteArray()).array();
}

void LocalGroups::setGroupsList(const QJsonArray &groups)
{
    m_settings->setValue(kGroups, QJsonDocument(groups).toJson(QJsonDocument::Compact));
    m_settings->sync();
}

QJsonObject LocalGroups::placement() const
{
    return QJsonDocument::fromJson(m_settings->value(kPlacement).toByteArray()).object();
}

void LocalGroups::setPlacement(const QJsonObject &placement)
{
    m_settings->setValue(kPlacement, QJsonDocument(placement).toJson(QJsonDocument::Compact));
    m_settings->sync();
}

void LocalGroups::apply()
{
    QHash<QString, QStringList> lightsByGroup;
    const QJsonObject places = placement();
    for (auto it = places.constBegin(); it != places.constEnd(); ++it) {
        for (const QJsonValue &group : it.value().toArray())
            lightsByGroup[group.toString()].append(it.key());
    }
    m_store->setExtraLights(lightsByGroup);

    QJsonArray resources;
    for (const QJsonValue &value : groups()) {
        const QJsonObject group = value.toObject();
        QJsonObject metadata;
        metadata.insert(QStringLiteral("name"), group.value(QStringLiteral("name")));
        metadata.insert(QStringLiteral("archetype"), group.value(QStringLiteral("archetype")));
        QJsonObject resource;
        resource.insert(QStringLiteral("id"), group.value(QStringLiteral("id")));
        resource.insert(QStringLiteral("type"), group.value(QStringLiteral("type")));
        resource.insert(QStringLiteral("metadata"), metadata);
        resource.insert(QStringLiteral("children"), QJsonArray());
        resource.insert(QStringLiteral("services"), QJsonArray());
        resources.append(resource);
    }
    m_store->reset(resources, Source);
}

QString LocalGroups::create(const QString &type, const QString &name, const QString &archetype)
{
    const QString id = Source + QLatin1Char(':') + QUuid::createUuid().toString().mid(1, 8);
    QJsonObject group;
    group.insert(QStringLiteral("id"), id);
    group.insert(QStringLiteral("type"), type == QLatin1String("zone") ? type : QStringLiteral("room"));
    group.insert(QStringLiteral("name"), name.trimmed().left(32));
    group.insert(QStringLiteral("archetype"), archetype.isEmpty() ? QStringLiteral("other") : archetype);
    QJsonArray list = groups();
    list.append(group);
    setGroupsList(list);
    apply();
    return id;
}

void LocalGroups::update(const QString &groupId, const QString &name, const QString &archetype)
{
    QJsonArray list = groups();
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject group = list.at(i).toObject();
        if (group.value(QStringLiteral("id")).toString() != groupId)
            continue;
        if (!name.trimmed().isEmpty())
            group.insert(QStringLiteral("name"), name.trimmed().left(32));
        if (!archetype.isEmpty())
            group.insert(QStringLiteral("archetype"), archetype);
        list.replace(i, group);
        setGroupsList(list);
        apply();
        return;
    }
}

void LocalGroups::remove(const QString &groupId)
{
    QJsonArray list = groups();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).toObject().value(QStringLiteral("id")).toString() == groupId)
            list.removeAt(i);
    }
    setGroupsList(list);
    QJsonObject places = placement();
    for (const QString &light : places.keys()) {
        QStringList groups = groupsOf(light);
        groups.removeAll(groupId);
        if (groups.isEmpty())
            places.remove(light);
        else
            places.insert(light, QJsonArray::fromStringList(groups));
    }
    setPlacement(places);
    apply();
}

QStringList LocalGroups::groupsOf(const QString &lightId) const
{
    QStringList list;
    for (const QJsonValue &group : placement().value(lightId).toArray())
        list.append(group.toString());
    return list;
}

void LocalGroups::setGroups(const QString &lightId, const QStringList &groupIds)
{
    QJsonObject places = placement();
    QStringList unique;
    for (const QString &id : groupIds) {
        if (!id.isEmpty() && !unique.contains(id))
            unique.append(id);
    }
    if (unique.isEmpty())
        places.remove(lightId);
    else
        places.insert(lightId, QJsonArray::fromStringList(unique));
    setPlacement(places);
    apply();
}

void LocalGroups::setMembers(const QString &groupId, const QStringList &candidates, const QStringList &members)
{
    QJsonObject places = placement();
    for (const QString &light : candidates) {
        QStringList groups;
        for (const QJsonValue &group : places.value(light).toArray())
            groups.append(group.toString());
        groups.removeAll(groupId);
        if (members.contains(light))
            groups.append(groupId);
        if (groups.isEmpty())
            places.remove(light);
        else
            places.insert(light, QJsonArray::fromStringList(groups));
    }
    setPlacement(places);
    apply();
}
