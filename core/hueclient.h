// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUECLIENT_H
#define HUECLIENT_H

#include "huebridge.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <QObject>
#include <QVariantList>

class HueCommandQueue;
class HueDiscovery;
class HuePairing;
class HueResourceStore;
class QNetworkAccessManager;
class QNetworkReply;
class QSettings;

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
    // The cover's two buttons: [{kind, id, name, archetype}, {...}], kind being "none", "alloff",
    // "group" (switch a room or zone on or off) or "scene".
    Q_PROPERTY(QVariantList coverActions READ coverActions NOTIFY coverActionsChanged)
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

    Q_INVOKABLE void pair(const QString &host, const QString &bridgeId, const QString &name);
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void forget();
    Q_INVOKABLE void reconnect();

    // id: a light, a fixture (all its lights) or a grouped_light (rooms and zones pass their
    // groupedLightId).
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

    // Zones: lights from any rooms, switched together. lightIds may hold fixtures.
    Q_INVOKABLE void createZone(const QString &name, const QString &archetype, const QStringList &lightIds);
    Q_INVOKABLE void updateZone(const QString &zoneId, const QString &name, const QString &archetype,
                                const QStringList &lightIds);
    Q_INVOKABLE void deleteZone(const QString &zoneId);
    // For editing zones: the lights of a room or zone, and of a fixture (or just the light).
    Q_INVOKABLE QStringList lightsOf(const QString &groupId) const;
    Q_INVOKABLE QStringList resolveLights(const QString &id) const;
    // A light's name is its device's (the light's own is a deprecated copy), so both change.
    Q_INVOKABLE void renameLight(const QString &lightId, const QString &name);
    Q_INVOKABLE void updateRoom(const QString &roomId, const QString &name, const QString &archetype);
    // Switch a sensor service (e.g. motion) on or off.
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void allOff();
    Q_INVOKABLE QColor mirekColor(int mirek) const;

    // Switches a room or zone off if any of its lights are on, otherwise on.
    Q_INVOKABLE void toggleGroup(const QString &groupId);

    QVariantList coverActions() const;
    Q_INVOKABLE void setCoverAction(int slot, const QString &kind, const QString &id = QString());
    Q_INVOKABLE void runCoverAction(int slot);

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

private:
    QJsonArray sceneActions(const QString &groupId) const;
    QJsonObject zoneBody(const QString &name, const QString &archetype, const QStringList &lightIds) const;
    void finishRequest(QNetworkReply *reply, const std::function<void(const QByteArray &body)> &done = nullptr);
    void command(const QString &target, const QJsonObject &body, const QJsonObject &optimistic = QJsonObject());
    void applyConfig();
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
    State m_state = NotConfigured;
    QString m_lastError;
    PairingState m_pairingState = PairingIdle;
    int m_pairingSecondsLeft = 0;
    QString m_pairingError;
    QString m_pairingHost, m_pairingId, m_pairingName;
    qint64 m_inactiveSince = 0;
};

#endif // HUECLIENT_H
