// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUEMODELS_H
#define HUEMODELS_H

#include "huelistmodel.h"

#include <QColor>

// Rooms, then zones (or just one of them: groupId); each with the state of its grouped_light.
class HueRoomModel : public HueListModel
{
    Q_OBJECT
    Q_PROPERTY(QString groupId READ groupId WRITE setGroupId NOTIFY groupIdChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        KindRole,           // "room" or "zone"
        NameRole,
        ArchetypeRole,
        GroupedLightIdRole,
        OnRole,
        BrightnessRole,     // 0-100
        LightCountRole,
        DimmableRole        // whether any of its lights dims
    };

    explicit HueRoomModel(QObject *parent = nullptr);
    QHash<int, QByteArray> roleNames() const override;

    QString groupId() const { return m_groupId; }
    void setGroupId(const QString &groupId);

signals:
    void groupIdChanged();

protected:
    QStringList collectIds() const override;
    QStringList dependenciesOf(const QString &id) const override;
    QVariant dataFor(const QString &id, int role) const override;

private:
    QString m_groupId;
};

// Lights of one room or zone (groupId), just one light (lightId), or all of them. Lamps with
// several spots are one row (a fixture, see HueResourceStore) unless groupFixtures is off; then a
// fixture's lightId lists its spots.
class HueLightModel : public HueListModel
{
    Q_OBJECT
    Q_PROPERTY(QString groupId READ groupId WRITE setGroupId NOTIFY groupIdChanged)
    Q_PROPERTY(QString lightId READ lightId WRITE setLightId NOTIFY lightIdChanged)
    Q_PROPERTY(bool groupFixtures READ groupFixtures WRITE setGroupFixtures NOTIFY groupFixturesChanged)
    // Only lights in no room, e.g. new Zigbee ones.
    Q_PROPERTY(bool withoutRoom READ withoutRoom WRITE setWithoutRoom NOTIFY withoutRoomChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ArchetypeRole,
        OnRole,
        BrightnessRole,
        ReachableRole,
        SupportsDimmingRole,
        SupportsColorRole,
        SupportsTemperatureRole,
        MirekRole,
        MirekMinRole,
        MirekMaxRole,
        DisplayColorRole,   // what the lamp roughly looks like, for swatches
        LightCountRole      // more than 1 for a fixture
    };

    explicit HueLightModel(QObject *parent = nullptr);
    QHash<int, QByteArray> roleNames() const override;

    QString groupId() const { return m_groupId; }
    void setGroupId(const QString &groupId);
    QString lightId() const { return m_lightId; }
    void setLightId(const QString &lightId);
    bool groupFixtures() const { return m_groupFixtures; }
    void setGroupFixtures(bool groupFixtures);
    bool withoutRoom() const { return m_withoutRoom; }
    void setWithoutRoom(bool withoutRoom);

    // Shared with HueClient: the colour a light shows right now.
    static QColor displayColor(const QJsonObject &light);

signals:
    void groupIdChanged();
    void lightIdChanged();
    void groupFixturesChanged();
    void withoutRoomChanged();

protected:
    QStringList collectIds() const override;
    QStringList dependenciesOf(const QString &id) const override;
    QVariant dataFor(const QString &id, int role) const override;

private:
    QVariant fixtureData(const QString &id, int role) const;

    QString m_groupId;
    QString m_lightId;
    bool m_groupFixtures = true;
    bool m_withoutRoom = false;
};

// Scenes of one room or zone.
class HueSceneModel : public HueListModel
{
    Q_OBJECT
    Q_PROPERTY(QString groupId READ groupId WRITE setGroupId NOTIFY groupIdChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ActiveRole,
        ColorRole           // a colour from the scene, for its tile
    };

    explicit HueSceneModel(QObject *parent = nullptr);
    QHash<int, QByteArray> roleNames() const override;

    QString groupId() const { return m_groupId; }
    void setGroupId(const QString &groupId);

signals:
    void groupIdChanged();

protected:
    QStringList collectIds() const override;
    QVariant dataFor(const QString &id, int role) const override;

private:
    QString m_groupId;
};

// Devices with sensors, buttons or batteries: motion sensors, switches, ...
class HueSensorModel : public HueListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ProductNameRole,
        KindRole,           // "motion", "switch", "contact" or "sensor"
        ReachableRole,
        MotionIdRole,       // the motion service, to switch it on or off; empty if none
        MotionEnabledRole,
        MotionRole,
        MotionChangedRole,  // when motion last started or stopped
        TemperatureRole,    // °C; undefined if none
        LightLevelRole,     // lux; undefined if none
        BatteryLevelRole,   // %; -1 if none
        BatteryLowRole,
        LastButtonRole,     // number of the button pressed last; 0 if none
        LastButtonEventRole,// short_release, long_press, ...
        LastButtonTimeRole
    };

    explicit HueSensorModel(QObject *parent = nullptr);
    QHash<int, QByteArray> roleNames() const override;

    // The bridge reports light level as 10000 * log10(lux) + 1.
    static double lux(int lightLevel);

protected:
    QStringList collectIds() const override;
    QStringList dependenciesOf(const QString &id) const override;
    QVariant dataFor(const QString &id, int role) const override;

private:
    QJsonObject service(const QString &deviceId, const QString &rtype) const;
    QJsonObject lastButton(const QString &deviceId) const;
};

// The bridge's automations (behavior_instance resources made from an "automation" script; not
// the ones behind switches): wake up, go to sleep, timers, lights on at a time, ... What a
// schedule's configuration holds depends on its script; the roles give what can be read from
// the common shapes, and stay empty (or -1) otherwise.
class HueScheduleModel : public HueListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,           // "wake_up", "go_to_sleep", "timer", "time_based_light", "natural_light",
                            // "coming_home", "leaving_home"; empty for other scripts
        ScriptNameRole,     // the script's name, as the bridge has it (in English)
        EnabledRole,
        ErrorRole,          // what went wrong, if the bridge says it did
        TimeTypeRole,       // "time", "sunrise", "sunset" or empty
        HourRole,           // when it starts, if TimeTypeRole is "time"; -1 otherwise
        MinuteRole,
        OffsetRole,         // minutes before (< 0) or after sunrise or sunset
        EndHourRole,        // when it ends, for those with an end time; -1 otherwise
        EndMinuteRole,
        DaysRole,           // "monday" ... "sunday"; empty: once
        DurationRole,       // a timer's, in seconds; -1 if none
        PlacesRole,         // names of the rooms and zones it's for
        EditableRole        // whether its time and days can be changed (setScheduleTime)
    };

    explicit HueScheduleModel(QObject *parent = nullptr);
    QHash<int, QByteArray> roleNames() const override;

    // Where a configuration says when: "when" (wake up, go to sleep) or "when_extended" (with an
    // end), and the start's time_point in it ({type: time, time: {hour, minute}}, or sunrise...).
    static QString whenKey(const QJsonObject &configuration);
    static QStringList startPath(const QString &whenKey);
    static const QStringList &weekDays();

protected:
    QStringList collectIds() const override;
    QStringList dependenciesOf(const QString &id) const override;
    QVariant dataFor(const QString &id, int role) const override;

private:
    QStringList groupsOf(const QJsonObject &instance) const;
};

#endif // HUEMODELS_H
