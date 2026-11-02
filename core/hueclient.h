// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUECLIENT_H
#define HUECLIENT_H

#include "huebridge.h"
// Not just declared: Qt 6 wants the types of pointer properties complete.
#include "huediscovery.h"
#include "hueresourcestore.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <QObject>
#include <QVariantList>

class HueCommandQueue;
class HuePairing;
class LocalGroups;
class MqttClient;
class QNetworkAccessManager;
class QNetworkReply;
class QSettings;
class Zigbee2Mqtt;

// Everything the UI needs, in one object: the paired bridge (remembered in settings), its live
// state in `store`, finding and pairing bridges, and commands.
//
// Commands change the store right away (so the UI follows the finger) and go to the bridge
// through a rate-limited queue; the event stream then confirms them. When the bridge rejects
// one, everything is loaded again to undo what the store assumed.
class HueClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ isConfigured NOTIFY configuredChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString bridgeName READ bridgeName NOTIFY configuredChanged)
    Q_PROPERTY(QString bridgeHost READ bridgeHost NOTIFY configuredChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(HueResourceStore *store READ store CONSTANT)
    Q_PROPERTY(HueDiscovery *discovery READ discovery CONSTANT)
    Q_PROPERTY(PairingState pairingState READ pairingState NOTIFY pairingChanged)
    Q_PROPERTY(int pairingSecondsLeft READ pairingSecondsLeft NOTIFY pairingChanged)
    Q_PROPERTY(QString pairingError READ pairingError NOTIFY pairingChanged)
    // "lampje#<device>", as the Hue app lists it; set before QML sees it.
    Q_PROPERTY(QString deviceType READ deviceType CONSTANT)
    // The cover's two buttons: [{kind, id, name, archetype}, {...}], kind being "none", "alloff",
    // "group" (switch a room or zone on or off) or "scene".
    Q_PROPERTY(QVariantList coverActions READ coverActions NOTIFY coverActionsChanged)
    // zigbee2mqtt, through an MQTT broker; its devices join the bridge's in `store`.
    Q_PROPERTY(bool mqttConfigured READ isMqttConfigured NOTIFY mqttConfigChanged)
    Q_PROPERTY(QString mqttHost READ mqttHost NOTIFY mqttConfigChanged)
    Q_PROPERTY(int mqttPort READ mqttPort NOTIFY mqttConfigChanged)
    Q_PROPERTY(QString mqttUser READ mqttUser NOTIFY mqttConfigChanged)
    Q_PROPERTY(QString mqttBaseTopic READ mqttBaseTopic NOTIFY mqttConfigChanged)
    Q_PROPERTY(MqttState mqttState READ mqttState NOTIFY mqttStateChanged)
    Q_PROPERTY(QString mqttError READ mqttError NOTIFY mqttStateChanged)
public:
    enum State {
        NotConfigured,
        Connecting,
        Connected,
        Reconnecting,
        Unauthorized,
        Untrusted
    };
    Q_ENUM(State)

    enum PairingState {
        PairingIdle,
        WaitingForButton,
        PairingFailed
    };
    Q_ENUM(PairingState)

    enum MqttState {
        MqttOff,
        MqttConnecting,
        MqttConnected,
        MqttReconnecting,
        MqttRefused
    };
    Q_ENUM(MqttState)

    // settingsPath: where to remember the bridge; empty for the app's config directory.
    explicit HueClient(const QString &settingsPath = QString(), QObject *parent = nullptr);
    ~HueClient() override;

    bool isConfigured() const;
    State state() const { return m_state; }
    QString bridgeName() const;
    QString bridgeHost() const;
    QString lastError() const { return m_lastError; }
    HueResourceStore *store() const { return m_store; }
    HueDiscovery *discovery() const { return m_discovery; }
    HueBridge *bridge() const { return m_bridge; }
    HueCommandQueue *queue() const { return m_queue; }
    PairingState pairingState() const { return m_pairingState; }
    int pairingSecondsLeft() const { return m_pairingSecondsLeft; }
    QString pairingError() const { return m_pairingError; }

    // What the bridge (and the Hue app's list of apps) calls this one: "lampje#<device>".
    QString deviceType() const { return QStringLiteral("lampje#") + m_deviceName; }
    void setDeviceName(const QString &device);

    Q_INVOKABLE void pair(const QString &host, const QString &bridgeId, const QString &name);
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void forget();
    Q_INVOKABLE void reconnect();

    // id: a light, a fixture (all its lights), a grouped_light, or a room or zone (its
    // grouped_light, or each of its lights if it has none, like the Zigbee room).
    Q_INVOKABLE void setOn(const QString &id, bool on);
    Q_INVOKABLE void setBrightness(const QString &id, double brightness);
    Q_INVOKABLE void setColor(const QString &id, const QColor &color);
    Q_INVOKABLE void setMirek(const QString &id, int mirek);
    Q_INVOKABLE void recallScene(const QString &sceneId);
    // A new scene in a room or zone with how its lights are now; sceneCreated() follows.
    Q_INVOKABLE void createScene(const QString &groupId, const QString &name);
    Q_INVOKABLE void renameScene(const QString &sceneId, const QString &name);
    // Make the scene what its room's lights are now.
    Q_INVOKABLE void storeScene(const QString &sceneId);
    Q_INVOKABLE void deleteScene(const QString &sceneId);

    // Zones: lights from any rooms, switched together. lightIds may hold fixtures. Zigbee lights
    // are added on the phone (the bridge can't hold them); a zone of only those is Lampje's own.
    Q_INVOKABLE void createZone(const QString &name, const QString &archetype, const QStringList &lightIds);
    Q_INVOKABLE void updateZone(const QString &zoneId, const QString &name, const QString &archetype,
                                const QStringList &lightIds);
    Q_INVOKABLE void deleteZone(const QString &zoneId);
    // For editing zones: the lights of a room or zone, and of a fixture (or just the light).
    Q_INVOKABLE QStringList lightsOf(const QString &groupId) const;
    Q_INVOKABLE QStringList resolveLights(const QString &id) const;
    // A light's name is its device's (the light's own is a deprecated copy), so both change.
    Q_INVOKABLE void renameLight(const QString &lightId, const QString &name);
    // The bridge's rooms, or Lampje's own rooms and zones.
    Q_INVOKABLE void updateRoom(const QString &roomId, const QString &name, const QString &archetype);
    // A room of Lampje's own, for lights the bridge doesn't know; returns its id.
    Q_INVOKABLE QString createRoom(const QString &name, const QString &archetype);
    // Only Lampje's own rooms and zones.
    Q_INVOKABLE void deleteGroup(const QString &groupId);
    // The room a light is in (empty if none), and putting a Zigbee light in one.
    Q_INVOKABLE QString roomOfLight(const QString &lightId) const;
    Q_INVOKABLE void setLightRoom(const QString &lightId, const QString &roomId);
    // Whether the bridge (rather than zigbee2mqtt or Lampje itself) has it.
    Q_INVOKABLE bool isFromBridge(const QString &id) const;
    // Switch a sensor service (e.g. motion) or a schedule (behavior_instance) on or off.
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    // A schedule that starts at a time of day (see HueScheduleModel's editable): when, and on
    // which days ("monday" ... "sunday"; none: once). The rest of its configuration stays.
    Q_INVOKABLE void setScheduleTime(const QString &scheduleId, int hour, int minute, const QStringList &days);
    Q_INVOKABLE void deleteSchedule(const QString &scheduleId);
    Q_INVOKABLE void allOff();
    Q_INVOKABLE QColor mirekColor(int mirek) const;

    // Switches a room or zone off if any of its lights are on, otherwise on.
    Q_INVOKABLE void toggleGroup(const QString &groupId);

    QVariantList coverActions() const;
    Q_INVOKABLE void setCoverAction(int slot, const QString &kind, const QString &id = QString());
    Q_INVOKABLE void runCoverAction(int slot);

    bool isMqttConfigured() const;
    QString mqttHost() const;
    int mqttPort() const;
    QString mqttUser() const;
    QString mqttBaseTopic() const;
    MqttState mqttState() const;
    QString mqttError() const { return m_mqttError; }
    Zigbee2Mqtt *zigbee() const { return m_zigbee; }
    MqttClient *mqtt() const { return m_mqtt; }
    // An empty password keeps the one there is (if the user stays the same).
    Q_INVOKABLE void setMqttConfig(const QString &host, int port, const QString &user,
                                   const QString &password, const QString &baseTopic);
    Q_INVOKABLE void forgetMqtt();

public slots:
    // From the application's state: after a while in the background (or asleep), the stream may
    // be dead without anyone noticing, so becoming active reconnects.
    void setApplicationActive(bool active);

signals:
    void configuredChanged();
    void stateChanged();
    void lastErrorChanged();
    void pairingChanged();
    void paired();
    void sceneCreated(const QString &sceneId);
    void coverActionsChanged();
    void mqttConfigChanged();
    void mqttStateChanged();

private:
    QJsonArray sceneActions(const QString &groupId) const;
    QJsonObject zoneBody(const QString &name, const QString &archetype, const QStringList &lightIds) const;
    void finishRequest(QNetworkReply *reply, const std::function<void(const QByteArray &body)> &done = nullptr);
    void command(const QString &target, const QJsonObject &body, const QJsonObject &optimistic = QJsonObject());
    bool isGroupOn(const QString &groupId) const;
    QStringList otherLights() const;
    // Like command(), but a room or zone stands for its lights.
    void lightCommand(const QString &target, const QJsonObject &body, const QJsonObject &optimistic = QJsonObject());
    void applyConfig();
    void applyMqttConfig();
    void updateState();
    void setLastError(const QString &error);
    void setPairing(PairingState state, int secondsLeft, const QString &error);

    QSettings *m_settings;
    QNetworkAccessManager *m_nam;
    HueResourceStore *m_store;
    HueBridge *m_bridge;
    HueCommandQueue *m_queue;
    HueDiscovery *m_discovery;
    HuePairing *m_pairing;
    MqttClient *m_mqtt;
    Zigbee2Mqtt *m_zigbee;
    LocalGroups *m_local;
    QString m_mqttError;
    State m_state = NotConfigured;
    QString m_lastError;
    PairingState m_pairingState = PairingIdle;
    int m_pairingSecondsLeft = 0;
    QString m_pairingError;
    QString m_pairingHost, m_pairingId, m_pairingName;
    qint64 m_inactiveSince = 0;
    QString m_deviceName = QStringLiteral("sailfishos");
};

#endif // HUECLIENT_H
