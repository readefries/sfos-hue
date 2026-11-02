// SPDX-License-Identifier: AGPL-3.0-or-later
#include "zigbee2mqtt.h"

#include "hueresourcestore.h"
#include "mqttclient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QtMath>

#include <cmath>

namespace {

const QString kLight = QStringLiteral("light");
const QString kDevice = QStringLiteral("device");

QJsonObject ref(const QString &rid, const QString &rtype)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("rid"), rid);
    obj.insert(QStringLiteral("rtype"), rtype);
    return obj;
}

QJsonObject service(const QString &id, const QString &type, const QString &deviceId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("type"), type);
    obj.insert(QStringLiteral("owner"), ref(deviceId, kDevice));
    return obj;
}

QJsonObject object(const QString &key, const QJsonValue &value)
{
    QJsonObject obj;
    obj.insert(key, value);
    return obj;
}

// "online", or {"state": "online"} since zigbee2mqtt 1.22.
QString stateOf(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject())
        return doc.object().value(QStringLiteral("state")).toString();
    return QString::fromUtf8(payload).trimmed();
}

} // namespace

const QString Zigbee2Mqtt::Source = QStringLiteral("z2m");

Zigbee2Mqtt::Zigbee2Mqtt(MqttClient *mqtt, HueResourceStore *store, QObject *parent)
    : QObject(parent)
    , m_mqtt(mqtt)
    , m_store(store)
{
    connect(m_mqtt, &MqttClient::messageReceived, this, &Zigbee2Mqtt::handleMessage);
    connect(m_mqtt, &MqttClient::connected, this, &Zigbee2Mqtt::onConnected);
    // Connected or not, devices are only reachable through the broker.
    connect(m_mqtt, &MqttClient::stateChanged, this, &Zigbee2Mqtt::publishAll);
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(150);
    connect(&m_flushTimer, &QTimer::timeout, this, &Zigbee2Mqtt::flush);
}

void Zigbee2Mqtt::setBaseTopic(const QString &baseTopic)
{
    QString base = baseTopic.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    m_base = base.isEmpty() ? QStringLiteral("zigbee2mqtt") : base;
}

void Zigbee2Mqtt::clear()
{
    m_devices.clear();
    m_states.clear();
    m_available.clear();
    m_motionChanged.clear();
    m_bridgeOnline = true;
    m_pending.clear();
    m_flushTimer.stop();
    m_store->clear(Source);
}

void Zigbee2Mqtt::command(const QString &lightId, const QJsonObject &body)
{
    if (m_mqtt->state() != MqttClient::Connected)
        return;
    for (const Device &device : m_devices) {
        for (const Light &light : device.lights) {
            if (light.id != lightId)
                continue;
            auto property = [&light](const char *name) {
                return light.properties.value(QString::fromLatin1(name)).toString();
            };
            QJsonObject set;
            const QJsonValue on = body.value(QStringLiteral("on")).toObject().value(QStringLiteral("on"));
            if (on.isBool())
                set.insert(property("state"), on.toBool() ? QStringLiteral("ON") : QStringLiteral("OFF"));
            const QJsonValue brightness = body.value(QStringLiteral("dimming")).toObject().value(QStringLiteral("brightness"));
            if (brightness.isDouble() && !property("brightness").isEmpty()) {
                set.insert(property("brightness"),
                           qBound(1, qRound(brightness.toDouble() * light.brightnessMax / 100.0), light.brightnessMax));
            }
            const QJsonValue mirek = body.value(QStringLiteral("color_temperature")).toObject().value(QStringLiteral("mirek"));
            QString colorMode;
            if (mirek.isDouble() && !property("color_temp").isEmpty()) {
                set.insert(property("color_temp"), qBound(light.mirekMin, mirek.toInt(), light.mirekMax));
                colorMode = QStringLiteral("color_temp");
            }
            const QJsonObject xy = body.value(QStringLiteral("color")).toObject().value(QStringLiteral("xy")).toObject();
            if (!xy.isEmpty() && !property("color").isEmpty()) {
                set.insert(property("color"), xy);
                colorMode = QStringLiteral("xy");
            }
            if (set.isEmpty())
                return;

            // The device reports its state after it changed; until then, assume it did.
            QJsonObject &state = m_states[device.name];
            for (auto it = set.constBegin(); it != set.constEnd(); ++it)
                state.insert(it.key(), it.value());
            if (!colorMode.isEmpty())
                state.insert(QStringLiteral("color_mode"), colorMode);
            updateDevice(device);

            QJsonObject &pending = m_pending[device.name];
            for (auto it = set.constBegin(); it != set.constEnd(); ++it)
                pending.insert(it.key(), it.value());
            if (!m_flushTimer.isActive())
                flush();
            return;
        }
    }
}

void Zigbee2Mqtt::flush()
{
    if (m_pending.isEmpty())
        return;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        m_mqtt->publish(m_base + QLatin1Char('/') + it.key() + QStringLiteral("/set"),
                        QJsonDocument(it.value()).toJson(QJsonDocument::Compact));
    }
    m_pending.clear();
    // Whatever comes in the meantime waits for the next round.
    m_flushTimer.start();
}

void Zigbee2Mqtt::onConnected()
{
    // Everything under the base topic: friendly names may contain slashes, so + won't do.
    m_mqtt->subscribe(m_base + QStringLiteral("/#"));
    // The retained device list follows; states are asked for once it's there.
}

void Zigbee2Mqtt::handleMessage(const QString &topic, const QByteArray &payload)
{
    const QString prefix = m_base + QLatin1Char('/');
    if (!topic.startsWith(prefix))
        return;
    const QString rest = topic.mid(prefix.size());
    if (rest == QLatin1String("bridge/devices")) {
        setDevices(QJsonDocument::fromJson(payload).array());
        return;
    }
    if (rest == QLatin1String("bridge/state")) {
        m_bridgeOnline = stateOf(payload) != QLatin1String("offline");
        publishAll();
        return;
    }
    if (rest.startsWith(QLatin1String("bridge/")))
        return;

    QString name = rest;
    const bool availability = rest.endsWith(QLatin1String("/availability"));
    if (availability)
        name.chop(int(qstrlen("/availability")));
    for (const Device &device : m_devices) {
        if (device.name != name)
            continue;
        if (availability) {
            m_available.insert(name, stateOf(payload) != QLatin1String("offline"));
        } else {
            const QJsonObject changes = QJsonDocument::fromJson(payload).object();
            if (changes.isEmpty())
                return;
            QJsonObject &state = m_states[name];
            const QString occupancy = QStringLiteral("occupancy");
            if (changes.contains(occupancy) && (!state.contains(occupancy)
                                                || state.value(occupancy) != changes.value(occupancy))) {
                m_motionChanged.insert(name, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            }
            for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
                state.insert(it.key(), it.value());
        }
        updateDevice(device);
        return;
    }
}

void Zigbee2Mqtt::setDevices(const QJsonArray &devices)
{
    static const QStringList sensors = {
        QStringLiteral("occupancy"), QStringLiteral("illuminance"), QStringLiteral("illuminance_lux"),
        QStringLiteral("temperature"), QStringLiteral("battery"),
    };
    m_devices.clear();
    QStringList names;
    for (const QJsonValue &value : devices) {
        const QJsonObject obj = value.toObject();
        const QJsonObject definition = obj.value(QStringLiteral("definition")).toObject();
        if (obj.value(QStringLiteral("type")).toString() == QLatin1String("Coordinator")
                || obj.value(QStringLiteral("disabled")).toBool() || definition.isEmpty()) {
            continue;
        }
        Device device;
        device.ieee = obj.value(QStringLiteral("ieee_address")).toString();
        device.name = obj.value(QStringLiteral("friendly_name")).toString();
        device.definition = definition;
        for (const QJsonValue &exposeValue : definition.value(QStringLiteral("exposes")).toArray()) {
            const QJsonObject expose = exposeValue.toObject();
            const QString type = expose.value(QStringLiteral("type")).toString();
            if (type != QLatin1String("light") && type != QLatin1String("switch")) {
                const QString property = expose.value(QStringLiteral("property")).toString();
                if (sensors.contains(property))
                    device.sensors.append(property);
                continue;
            }
            Light light;
            light.endpoint = expose.value(QStringLiteral("endpoint")).toString();
            light.id = QStringLiteral("z2m:%1:light").arg(device.ieee);
            if (!light.endpoint.isEmpty())
                light.id += QLatin1Char(':') + light.endpoint;
            for (const QJsonValue &featureValue : expose.value(QStringLiteral("features")).toArray()) {
                const QJsonObject feature = featureValue.toObject();
                const QString name = feature.value(QStringLiteral("name")).toString();
                const QString property = feature.value(QStringLiteral("property")).toString();
                if (name == QLatin1String("state")) {
                    light.properties.insert(QStringLiteral("state"), property);
                } else if (name == QLatin1String("brightness")) {
                    light.properties.insert(QStringLiteral("brightness"), property);
                    light.brightnessMax = feature.value(QStringLiteral("value_max")).toInt(254);
                } else if (name == QLatin1String("color_temp")) {
                    light.properties.insert(QStringLiteral("color_temp"), property);
                    light.mirekMin = feature.value(QStringLiteral("value_min")).toInt(153);
                    light.mirekMax = feature.value(QStringLiteral("value_max")).toInt(500);
                } else if (name == QLatin1String("color_xy")) {
                    light.properties.insert(QStringLiteral("color"), property);
                }
            }
            if (light.properties.contains(QStringLiteral("state")))
                device.lights.append(light);
        }
        if (!device.lights.isEmpty() || !device.sensors.isEmpty()) {
            m_devices.append(device);
            names.append(device.name);
        }
    }
    // Devices that left (or were renamed) take their state with them.
    for (const QString &name : m_states.keys()) {
        if (!names.contains(name))
            m_states.remove(name);
    }

    QJsonArray resources;
    for (const Device &device : m_devices) {
        for (const QJsonObject &resource : resourcesOf(device))
            resources.append(resource);
    }
    m_store->reset(resources, Source);
    requestStates();
}

void Zigbee2Mqtt::requestStates()
{
    for (const Device &device : m_devices) {
        if (!m_states.contains(device.name))
            requestState(device);
    }
}

void Zigbee2Mqtt::requestState(const Device &device)
{
    // Battery powered sensors sleep and don't answer; they report when something changes.
    QJsonObject get;
    for (const Light &light : device.lights) {
        for (const QJsonValue &property : light.properties)
            get.insert(property.toString(), QString());
    }
    if (get.isEmpty())
        return;
    m_mqtt->publish(m_base + QLatin1Char('/') + device.name + QStringLiteral("/get"),
                    QJsonDocument(get).toJson(QJsonDocument::Compact));
}

void Zigbee2Mqtt::updateDevice(const Device &device)
{
    for (const QJsonObject &resource : resourcesOf(device)) {
        const QString id = resource.value(QStringLiteral("id")).toString();
        if (m_store->resource(id) != resource)
            m_store->add(resource);
    }
}

void Zigbee2Mqtt::publishAll()
{
    for (const Device &device : m_devices)
        updateDevice(device);
}

bool Zigbee2Mqtt::isReachable(const Device &device) const
{
    return m_mqtt->state() == MqttClient::Connected && m_bridgeOnline
            && m_available.value(device.name, true);
}

QList<QJsonObject> Zigbee2Mqtt::resourcesOf(const Device &device) const
{
    const QString deviceId = QStringLiteral("z2m:") + device.ieee;
    const QJsonObject state = m_states.value(device.name);
    QList<QJsonObject> resources;
    QJsonArray services;

    for (const Light &light : device.lights) {
        resources.append(lightResource(device, light));
        services.append(ref(light.id, kLight));
    }

    QJsonObject connectivity = service(deviceId + QStringLiteral(":connectivity"),
                                       QStringLiteral("zigbee_connectivity"), deviceId);
    connectivity.insert(QStringLiteral("status"), isReachable(device) ? QStringLiteral("connected")
                                                                      : QStringLiteral("connectivity_issue"));
    resources.append(connectivity);
    services.append(ref(connectivity.value(QStringLiteral("id")).toString(), QStringLiteral("zigbee_connectivity")));

    auto addSensor = [&](const QString &type, const QString &key, const QJsonObject &value) {
        QJsonObject resource = service(deviceId + QLatin1Char(':') + type, type, deviceId);
        resource.insert(key, value);
        // Switching motion detection off is the bridge's thing; here it's always on.
        if (type == QLatin1String("motion"))
            resource.insert(QStringLiteral("enabled"), true);
        resources.append(resource);
        services.append(ref(resource.value(QStringLiteral("id")).toString(), type));
    };
    if (device.sensors.contains(QStringLiteral("occupancy"))) {
        const QJsonValue occupancy = state.value(QStringLiteral("occupancy"));
        QJsonObject motion;
        motion.insert(QStringLiteral("motion"), occupancy.toBool());
        motion.insert(QStringLiteral("motion_valid"), occupancy.isBool());
        if (m_motionChanged.contains(device.name)) {
            QJsonObject report;
            report.insert(QStringLiteral("changed"), m_motionChanged.value(device.name));
            report.insert(QStringLiteral("motion"), occupancy.toBool());
            motion.insert(QStringLiteral("motion_report"), report);
        }
        addSensor(QStringLiteral("motion"), QStringLiteral("motion"), motion);
    }
    const QString lux = device.sensors.contains(QStringLiteral("illuminance_lux"))
            ? QStringLiteral("illuminance_lux") : QStringLiteral("illuminance");
    if (device.sensors.contains(lux)) {
        const QJsonValue value = state.value(lux);
        QJsonObject light;
        // Like the bridge: 10000 * log10(lux) + 1.
        light.insert(QStringLiteral("light_level"),
                     value.toDouble() > 0 ? qRound(10000 * std::log10(value.toDouble()) + 1) : 0);
        light.insert(QStringLiteral("light_level_valid"), value.isDouble());
        addSensor(QStringLiteral("light_level"), QStringLiteral("light"), light);
    }
    if (device.sensors.contains(QStringLiteral("temperature"))) {
        const QJsonValue value = state.value(QStringLiteral("temperature"));
        QJsonObject temperature;
        temperature.insert(QStringLiteral("temperature"), value.toDouble());
        temperature.insert(QStringLiteral("temperature_valid"), value.isDouble());
        addSensor(QStringLiteral("temperature"), QStringLiteral("temperature"), temperature);
    }
    if (device.sensors.contains(QStringLiteral("battery"))) {
        const QJsonValue value = state.value(QStringLiteral("battery"));
        QJsonObject power;
        if (value.isDouble()) {
            const int level = value.toInt();
            power.insert(QStringLiteral("battery_level"), level);
            power.insert(QStringLiteral("battery_state"),
                         level <= 5 ? QStringLiteral("critical") : level <= 15 ? QStringLiteral("low")
                                                                               : QStringLiteral("normal"));
        }
        addSensor(QStringLiteral("device_power"), QStringLiteral("power_state"), power);
    }

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), device.name);
    metadata.insert(QStringLiteral("archetype"), QStringLiteral("classic_bulb"));
    QJsonObject productData;
    productData.insert(QStringLiteral("model_id"), device.definition.value(QStringLiteral("model")));
    productData.insert(QStringLiteral("manufacturer_name"), device.definition.value(QStringLiteral("vendor")));
    productData.insert(QStringLiteral("product_name"), device.definition.value(QStringLiteral("description")));
    QJsonObject deviceResource;
    deviceResource.insert(QStringLiteral("id"), deviceId);
    deviceResource.insert(QStringLiteral("type"), kDevice);
    deviceResource.insert(QStringLiteral("metadata"), metadata);
    deviceResource.insert(QStringLiteral("product_data"), productData);
    deviceResource.insert(QStringLiteral("services"), services);
    resources.prepend(deviceResource);
    return resources;
}

QJsonObject Zigbee2Mqtt::lightResource(const Device &device, const Light &light) const
{
    const QJsonObject state = m_states.value(device.name);
    auto valueOf = [&](const char *property) {
        return state.value(light.properties.value(QString::fromLatin1(property)).toString());
    };
    QJsonObject resource = service(light.id, kLight, QStringLiteral("z2m:") + device.ieee);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), light.endpoint.isEmpty() ? device.name
                                                                     : device.name + QLatin1Char(' ') + light.endpoint);
    metadata.insert(QStringLiteral("archetype"), QStringLiteral("classic_bulb"));
    resource.insert(QStringLiteral("metadata"), metadata);
    resource.insert(QStringLiteral("on"), object(QStringLiteral("on"),
                                                 valueOf("state").toString() == QLatin1String("ON")));

    if (light.properties.contains(QStringLiteral("brightness"))) {
        const double raw = valueOf("brightness").toDouble();
        const double brightness = qBound(0.0, raw * 100.0 / qMax(1, light.brightnessMax), 100.0);
        resource.insert(QStringLiteral("dimming"),
                        object(QStringLiteral("brightness"), qRound(brightness * 100) / 100.0));
    }
    const bool hasColor = light.properties.contains(QStringLiteral("color"));
    if (light.properties.contains(QStringLiteral("color_temp"))) {
        const QJsonValue mirek = valueOf("color_temp");
        QJsonObject schema;
        schema.insert(QStringLiteral("mirek_minimum"), light.mirekMin);
        schema.insert(QStringLiteral("mirek_maximum"), light.mirekMax);
        QJsonObject ct;
        ct.insert(QStringLiteral("mirek"), mirek.isDouble() ? mirek : QJsonValue());
        ct.insert(QStringLiteral("mirek_valid"), mirek.isDouble() && (!hasColor
                  || state.value(QStringLiteral("color_mode")).toString() == QLatin1String("color_temp")));
        ct.insert(QStringLiteral("mirek_schema"), schema);
        resource.insert(QStringLiteral("color_temperature"), ct);
    }
    if (hasColor) {
        const QJsonObject color = valueOf("color").toObject();
        QJsonObject xy;
        xy.insert(QStringLiteral("x"), color.value(QStringLiteral("x")).toDouble(0.4573));
        xy.insert(QStringLiteral("y"), color.value(QStringLiteral("y")).toDouble(0.41));
        resource.insert(QStringLiteral("color"), object(QStringLiteral("xy"), xy));
    }
    return resource;
}
