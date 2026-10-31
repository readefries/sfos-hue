// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef LOCALGROUPS_H
#define LOCALGROUPS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class HueResourceStore;
class QSettings;

// What Lampje adds to the bridge's rooms and zones, remembered in settings:
// - rooms and zones of its own ("local:<id>"), for places the bridge has no lights in, such as an
//   office with only Zigbee switches;
// - which rooms and zones lights the bridge doesn't know (Zigbee ones) are in, its own or the
//   bridge's.
// Both go into the store: the groups as resources without a grouped_light, the lights as its
// extra lights.
class LocalGroups : public QObject
{
    Q_OBJECT
public:
    static const QString Source;    // "local"

    LocalGroups(QSettings *settings, HueResourceStore *store, QObject *parent = nullptr);

    // Into the store, from settings.
    void apply();

    // type: "room" or "zone". Returns its id.
    QString create(const QString &type, const QString &name, const QString &archetype);
    void update(const QString &groupId, const QString &name, const QString &archetype);
    // Also takes it out of every light's groups.
    void remove(const QString &groupId);

    // Rooms and zones a light was put in.
    QStringList groupsOf(const QString &lightId) const;
    void setGroups(const QString &lightId, const QStringList &groupIds);
    // Of these candidates, exactly `members` are in the group afterwards (e.g. a zone's lights,
    // as picked in its dialog).
    void setMembers(const QString &groupId, const QStringList &candidates, const QStringList &members);

private:
    QJsonArray groups() const;
    void setGroupsList(const QJsonArray &groups);
    QJsonObject placement() const;
    void setPlacement(const QJsonObject &placement);

    QSettings *m_settings;
    HueResourceStore *m_store;
};

#endif // LOCALGROUPS_H
