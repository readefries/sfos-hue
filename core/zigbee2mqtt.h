// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef ZIGBEE2MQTT_H
#define ZIGBEE2MQTT_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class HueResourceStore;
class MqttClient;

// zigbee2mqtt's devices, in the store as if the bridge had them: ids "z2m:<ieee address>...".
//
// Each device becomes a `device`, with a zigbee_connectivity service for its availability. Each
// light or switch it exposes becomes a `light` (a switch with two channels gives two, "z2m:
// <ieee>:light:left" and ":right"), with only on/off, or also dimming, colour temperature and
// colour as far as it has those. Sensors become motion, light_level, temperature and
// device_power services. Which rooms they're in is up to LocalGroups.
//
// zigbee2mqtt publishes its device list retained on <base>/bridge/devices and each device's
// state on <base>/<friendly name>; the state isn't always retained, so it's asked for with
// <base>/<friendly name>/get.
class Zigbee2Mqtt : public QObject
{
    Q_OBJECT
public:
    static const QString Source;    // "z2m"

    Zigbee2Mqtt(MqttClient *mqtt, HueResourceStore *store, QObject *parent = nullptr);

    void setBaseTopic(const QString &baseTopic);
    QString baseTopic() const { return m_base; }

    // Forget all devices, e.g. when no longer connecting to a broker.
    void clear();

    // A command for one of its lights, as the bridge would take it: on, dimming, color_temperature
    // and color (xy). The store follows right away. Commands within the throttle interval are
    // merged and sent together, so a slider being dragged doesn't flood the Zigbee network.
    void command(const QString &lightId, const QJsonObject &body);
    void setThrottle(int msecs) { m_flushTimer.setInterval(msecs); }

    // For tests and the fake broker: what a message does.
    void handleMessage(const QString &topic, const QByteArray &payload);

private:
    struct Light {
        QString id;
        QString endpoint;   // empty for a device with one light
        QJsonObject properties; // state, brightness, ... -> the payload key, e.g. state_left
        int brightnessMax = 254;
        int mirekMin = 0, mirekMax = 0;
    };
    struct Device {
        QString ieee;
        QString name;
        QJsonObject definition;
        QList<Light> lights;
        QStringList sensors;   // occupancy, illuminance, temperature, battery
    };

    void onConnected();
    void setDevices(const QJsonArray &devices);
    void requestStates();
    void requestState(const Device &device);
    void updateDevice(const Device &device);
    QList<QJsonObject> resourcesOf(const Device &device) const;
    QJsonObject lightResource(const Device &device, const Light &light) const;
    bool isReachable(const Device &device) const;
    void publishAll();
    void flush();

    MqttClient *m_mqtt;
    HueResourceStore *m_store;
    QString m_base = QStringLiteral("zigbee2mqtt");
    QList<Device> m_devices;
    QHash<QString, QJsonObject> m_states;       // by friendly name
    QHash<QString, bool> m_available;           // by friendly name; unknown: available
    QHash<QString, QString> m_motionChanged;    // by friendly name, when occupancy last changed
    bool m_bridgeOnline = true;
    QHash<QString, QJsonObject> m_pending;      // by friendly name, what's to be set
    QTimer m_flushTimer;
};

#endif // ZIGBEE2MQTT_H
