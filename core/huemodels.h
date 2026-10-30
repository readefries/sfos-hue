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

// Lights of one room or zone (groupId), just one light (lightId), or all of them.
class HueLightModel : public HueListModel
{
    Q_OBJECT
    Q_PROPERTY(QString groupId READ groupId WRITE setGroupId NOTIFY groupIdChanged)
    Q_PROPERTY(QString lightId READ lightId WRITE setLightId NOTIFY lightIdChanged)
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
        DisplayColorRole    // what the lamp roughly looks like, for swatches
    };

    explicit HueLightModel(QObject *parent = nullptr);
    QHash<int, QByteArray> roleNames() const override;

    QString groupId() const { return m_groupId; }
    void setGroupId(const QString &groupId);
    QString lightId() const { return m_lightId; }
    void setLightId(const QString &lightId);

    // Shared with HueClient: the colour a light shows right now.
    static QColor displayColor(const QJsonObject &light);

signals:
    void groupIdChanged();
    void lightIdChanged();

protected:
    QStringList collectIds() const override;
    QStringList dependenciesOf(const QString &id) const override;
    QVariant dataFor(const QString &id, int role) const override;

private:
    QString m_groupId;
    QString m_lightId;
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

#endif // HUEMODELS_H
