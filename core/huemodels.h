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
        LightCountRole
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

    // Shared with HueClient: the colour a light shows right now.
    static QColor displayColor(const QJsonObject &light);

signals:
    void groupIdChanged();
    void lightIdChanged();
    void groupFixturesChanged();

protected:
    QStringList collectIds() const override;
    QStringList dependenciesOf(const QString &id) const override;
    QVariant dataFor(const QString &id, int role) const override;

private:
    QVariant fixtureData(const QString &id, int role) const;

    QString m_groupId;
    QString m_lightId;
    bool m_groupFixtures = true;
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

#endif // HUEMODELS_H
