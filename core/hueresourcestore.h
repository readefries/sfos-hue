// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUERESOURCESTORE_H
#define HUERESOURCESTORE_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

// Every CLIP v2 resource of one bridge, by id: filled from GET /clip/v2/resource and kept up to
// date by the event stream (and by optimistic local updates, which the stream later confirms).
//
// v2 resources reference each other: a room lists devices as children and has a grouped_light
// service, a device owns light and zigbee_connectivity services, a zone lists lights directly.
// The relation helpers below resolve those.
class HueResourceStore : public QObject
{
    Q_OBJECT
public:
    explicit HueResourceStore(QObject *parent = nullptr);

    // Replace everything, e.g. with the `data` array of GET /clip/v2/resource.
    void reset(const QJsonArray &resources);
    void clear();
    // The `data` of one event stream message: [{type: update|add|delete|error, data: [...]}, ...]
    void applyEvents(const QJsonArray &events);
    // Merge a partial resource ({id, type, ...changes}) into the one with that id.
    void applyUpdate(const QJsonObject &partial);
    void add(const QJsonObject &resource);
    void remove(const QString &id);

    bool isEmpty() const { return m_resources.isEmpty(); }
    bool contains(const QString &id) const { return m_resources.contains(id); }
    QJsonObject resource(const QString &id) const { return m_resources.value(id); }
    QString type(const QString &id) const;
    QString name(const QString &id) const;
    QStringList idsOfType(const QString &type) const;

    // grouped_light service of a room or zone.
    QString groupedLightOf(const QString &groupId) const;
    // The room or zone a grouped_light belongs to.
    QString groupOfGroupedLight(const QString &groupedLightId) const;
    // Lights in a room (through its devices) or zone (listed directly, or through devices).
    QStringList lightsOf(const QString &groupId) const;
    // zigbee_connectivity service of the device that owns a light; empty if none (e.g. a lamp
    // connected over something else, or the bridge itself).
    QString connectivityOfLight(const QString &lightId) const;
    // Whether the light is reachable: unknown counts as reachable.
    bool isReachable(const QString &lightId) const;
    QStringList scenesOf(const QString &groupId) const;

    // Lamps with several spots (e.g. Hue Runner) are separate devices, one light each; only their
    // model ids tell they belong together: the lamp's 12NC product number plus _01, _02, ...
    // A fixture id ("fixture:<12NC>:<room or zone id>") stands for those lights within one group
    // (or all lights, with an empty group), and works wherever a light id does.
    static bool isFixtureId(const QString &id);
    static QString fixtureId(const QString &productNumber, const QString &groupId);
    // The product number of the lamp a light is part of; empty for a light on its own.
    QString fixtureProductOf(const QString &lightId) const;
    QStringList lightsOfFixture(const QString &fixtureId) const;
    // lights, with the members of each fixture of two or more replaced by the fixture's id.
    QStringList withFixtures(const QStringList &lights, const QString &groupId) const;
    // A fixture's lights, or just the id itself.
    QStringList resolveLights(const QString &id) const;

signals:
    void resourcesReset();
    void resourceAdded(const QString &id, const QString &type);
    void resourceChanged(const QString &id, const QString &type);
    void resourceRemoved(const QString &id, const QString &type);

private:
    QHash<QString, QJsonObject> m_resources;
};

#endif // HUERESOURCESTORE_H
