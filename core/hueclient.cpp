// SPDX-License-Identifier: AGPL-3.0-or-later
#include "hueclient.h"

#include "huecolor.h"
#include "huecommandqueue.h"
#include "huediscovery.h"
#include "huejson.h"
#include "huepairing.h"
#include "hueresourcestore.h"

#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

const QString kId = QStringLiteral("bridge/id");
const QString kHost = QStringLiteral("bridge/host");
const QString kName = QStringLiteral("bridge/name");
const QString kAppKey = QStringLiteral("bridge/appKey");
const QString kCert = QStringLiteral("bridge/certSha256");

// Reconnect when coming back after this long; the stream might have died unnoticed.
const qint64 kStaleAfterMsecs = 60 * 1000;

QString defaultSettingsPath()
{
    // Sailjail only gives the app its own directory under ~/.config/<org>/<app>/, so not the
    // usual <app>.conf next to it.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/lampje.conf");
}

QJsonObject object(const QString &key, const QJsonValue &value)
{
    QJsonObject obj;
    obj.insert(key, value);
    return obj;
}

} // namespace

HueClient::HueClient(const QString &settingsPath, QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(settingsPath.isEmpty() ? defaultSettingsPath() : settingsPath,
                               QSettings::IniFormat, this))
    , m_nam(new QNetworkAccessManager(this))
    , m_store(new HueResourceStore(this))
    , m_bridge(new HueBridge(m_nam, m_store, this))
    , m_queue(new HueCommandQueue(m_bridge, this))
    , m_discovery(new HueDiscovery(m_nam, this))
    , m_pairing(new HuePairing(m_nam, this))
{
    connect(m_bridge, &HueBridge::stateChanged, this, &HueClient::updateState);

    connect(m_bridge, &HueBridge::errorOccurred, this, &HueClient::setLastError);
    connect(m_queue, &HueCommandQueue::commandFailed, this,
            [this](const QString &, const QString &, const QString &error) {
        setLastError(error);
        m_bridge->reload();
    });

    connect(m_pairing, &HuePairing::waitingForButton, this, [this](int secondsLeft) {
        setPairing(WaitingForButton, secondsLeft, QString());
    });
    connect(m_pairing, &HuePairing::failed, this, [this](const QString &error) {
        setPairing(PairingFailed, 0, error);
    });
    connect(m_pairing, &HuePairing::paired, this, [this](const QString &appKey, const QByteArray &cert) {
        m_settings->setValue(kId, m_pairingId);
        m_settings->setValue(kHost, m_pairingHost);
        m_settings->setValue(kName, m_pairingName);
        m_settings->setValue(kAppKey, appKey);
        m_settings->setValue(kCert, QString::fromLatin1(cert.toHex()));
        m_settings->sync();
        setPairing(PairingIdle, 0, QString());
        setLastError(QString());
        applyConfig();
        emit configuredChanged();
        emit paired();
    });

    applyConfig();
}

HueClient::~HueClient()
{
    m_bridge->stop();
}

bool HueClient::isConfigured() const
{
    return !m_settings->value(kAppKey).toString().isEmpty();
}

QString HueClient::bridgeName() const
{
    return m_settings->value(kName).toString();
}

QString HueClient::bridgeHost() const
{
    return m_settings->value(kHost).toString();
}

void HueClient::applyConfig()
{
    m_bridge->stop();
    m_queue->clear();
    m_store->clear();
    if (isConfigured()) {
        HueBridgeConfig config;
        config.baseUrl = HueDiscovery::baseUrlFor(bridgeHost());
        config.appKey = m_settings->value(kAppKey).toString();
        config.certSha256 = QByteArray::fromHex(m_settings->value(kCert).toString().toLatin1());
        m_bridge->setConfig(config);
        m_bridge->start();
    } else {
        m_bridge->setConfig(HueBridgeConfig());
    }
    updateState();
}

void HueClient::updateState()
{
    State state = NotConfigured;
    if (isConfigured()) {
        switch (m_bridge->state()) {
        case HueBridge::Idle:
        case HueBridge::Connecting: state = Connecting; break;
        case HueBridge::Connected: state = Connected; break;
        case HueBridge::WaitingToReconnect: state = Reconnecting; break;
        case HueBridge::Unauthorized: state = Unauthorized; break;
        case HueBridge::Untrusted: state = Untrusted; break;
        }
    }
    if (state == Connected)
        setLastError(QString());
    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

void HueClient::pair(const QString &host, const QString &bridgeId, const QString &name)
{
    m_pairingHost = host.trimmed();
    m_pairingId = bridgeId;
    m_pairingName = name;
    setPairing(WaitingForButton, 60, QString());
    m_pairing->start(HueDiscovery::baseUrlFor(m_pairingHost), QStringLiteral("lampje#sailfishos"));
}

void HueClient::cancelPairing()
{
    m_pairing->cancel();
    setPairing(PairingIdle, 0, QString());
}

void HueClient::forget()
{
    // The key stays valid on the bridge; it can only be removed in the official app.
    m_settings->remove(QStringLiteral("bridge"));
    m_settings->sync();
    setLastError(QString());
    applyConfig();
    emit configuredChanged();
}

void HueClient::reconnect()
{
    if (isConfigured())
        m_bridge->reconnectNow();
}

void HueClient::setApplicationActive(bool active)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!active) {
        m_inactiveSince = now;
        return;
    }
    if (m_inactiveSince > 0 && now - m_inactiveSince > kStaleAfterMsecs && isConfigured())
        m_bridge->reconnectNow();
    m_inactiveSince = 0;
}

void HueClient::command(const QString &target, const QJsonObject &body, const QJsonObject &optimistic)
{
    if (HueResourceStore::isFixtureId(target)) {
        for (const QString &light : m_store->resolveLights(target))
            command(light, body, optimistic);
        return;
    }
    const QString &id = target;
    const QString type = m_store->type(id);
    if (type.isEmpty())
        return;
    QJsonObject local = HueJson::merged(body, optimistic);
    local.insert(QStringLiteral("id"), id);
    local.insert(QStringLiteral("type"), type);
    m_store->applyUpdate(local);
    m_queue->enqueue(type, id, body);
}

void HueClient::setOn(const QString &id, bool on)
{
    command(id, object(QStringLiteral("on"), object(QStringLiteral("on"), on)));
}

void HueClient::setBrightness(const QString &id, double brightness)
{
    QJsonObject body = object(QStringLiteral("dimming"),
                              object(QStringLiteral("brightness"), qBound(1.0, brightness, 100.0)));
    // Dimming a light that's off leaves it off; nobody means that.
    body.insert(QStringLiteral("on"), object(QStringLiteral("on"), true));
    command(id, body);
}

void HueClient::setColor(const QString &id, const QColor &color)
{
    // Each spot of a fixture clamps to its own gamut.
    if (HueResourceStore::isFixtureId(id)) {
        for (const QString &light : m_store->resolveLights(id))
            setColor(light, color);
        return;
    }
    const QJsonObject resource = m_store->resource(id);
    QPointF xy = HueColor::rgbToXy(color);
    const QJsonValue gamut = HueJson::at(resource, {QStringLiteral("color"), QStringLiteral("gamut")});
    if (gamut.isObject())
        xy = HueColor::clampToGamut(xy, HueColor::gamutFromJson(gamut.toObject()));
    QJsonObject point;
    point.insert(QStringLiteral("x"), qRound(xy.x() * 10000) / 10000.0);
    point.insert(QStringLiteral("y"), qRound(xy.y() * 10000) / 10000.0);
    QJsonObject body = object(QStringLiteral("color"), object(QStringLiteral("xy"), point));
    body.insert(QStringLiteral("on"), object(QStringLiteral("on"), true));
    // The lamp leaves colour temperature mode; the stream says so too, but later.
    command(id, body, object(QStringLiteral("color_temperature"), object(QStringLiteral("mirek_valid"), false)));
}

void HueClient::setMirek(const QString &id, int mirek)
{
    QJsonObject body = object(QStringLiteral("color_temperature"), object(QStringLiteral("mirek"), mirek));
    body.insert(QStringLiteral("on"), object(QStringLiteral("on"), true));
    command(id, body, object(QStringLiteral("color_temperature"), object(QStringLiteral("mirek_valid"), true)));
}

void HueClient::recallScene(const QString &sceneId)
{
    if (m_store->type(sceneId) != QLatin1String("scene"))
        return;
    m_queue->enqueue(QStringLiteral("scene"), sceneId,
                     object(QStringLiteral("recall"), object(QStringLiteral("action"), QStringLiteral("active"))));
}

QJsonArray HueClient::sceneActions(const QString &groupId) const
{
    // Each light as it is: off, or on with its brightness and either its colour temperature or
    // its colour, whichever it's showing.
    QJsonArray actions;
    for (const QString &lightId : m_store->lightsOf(groupId)) {
        const QJsonObject light = m_store->resource(lightId);
        const bool on = HueJson::at(light, {QStringLiteral("on"), QStringLiteral("on")}).toBool();
        QJsonObject action = object(QStringLiteral("on"), object(QStringLiteral("on"), on));
        if (on) {
            const QJsonValue brightness = HueJson::at(light, {QStringLiteral("dimming"), QStringLiteral("brightness")});
            if (brightness.isDouble())
                action.insert(QStringLiteral("dimming"), object(QStringLiteral("brightness"), brightness));
            const QJsonObject ct = light.value(QStringLiteral("color_temperature")).toObject();
            const QJsonValue xy = HueJson::at(light, {QStringLiteral("color"), QStringLiteral("xy")});
            if (ct.value(QStringLiteral("mirek_valid")).toBool() && ct.value(QStringLiteral("mirek")).isDouble())
                action.insert(QStringLiteral("color_temperature"), object(QStringLiteral("mirek"), ct.value(QStringLiteral("mirek"))));
            else if (xy.isObject())
                action.insert(QStringLiteral("color"), object(QStringLiteral("xy"), xy));
        }
        QJsonObject target;
        target.insert(QStringLiteral("rid"), lightId);
        target.insert(QStringLiteral("rtype"), QStringLiteral("light"));
        QJsonObject entry;
        entry.insert(QStringLiteral("target"), target);
        entry.insert(QStringLiteral("action"), action);
        actions.append(entry);
    }
    return actions;
}

void HueClient::finishRequest(QNetworkReply *reply, const std::function<void(const QByteArray &)> &done)
{
    QTimer::singleShot(10 * 1000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const QString error = HueBridge::errorOf(reply, body);
        if (!error.isEmpty())
            setLastError(error);
        else if (done)
            done(body);
    });
}

void HueClient::createScene(const QString &groupId, const QString &name)
{
    const QString groupType = m_store->type(groupId);
    if (groupType != QLatin1String("room") && groupType != QLatin1String("zone"))
        return;
    QJsonObject group;
    group.insert(QStringLiteral("rid"), groupId);
    group.insert(QStringLiteral("rtype"), groupType);
    QJsonObject body;
    body.insert(QStringLiteral("type"), QStringLiteral("scene"));
    body.insert(QStringLiteral("metadata"), object(QStringLiteral("name"), name.trimmed().left(32)));
    body.insert(QStringLiteral("group"), group);
    body.insert(QStringLiteral("actions"), sceneActions(groupId));
    finishRequest(m_bridge->post(QStringLiteral("scene"), body), [this](const QByteArray &reply) {
        // The stream adds the scene itself.
        emit sceneCreated(HueBridge::createdId(reply));
    });
}

void HueClient::renameScene(const QString &sceneId, const QString &name)
{
    if (m_store->type(sceneId) == QLatin1String("scene"))
        command(sceneId, object(QStringLiteral("metadata"), object(QStringLiteral("name"), name.trimmed().left(32))));
}

void HueClient::storeScene(const QString &sceneId)
{
    if (m_store->type(sceneId) != QLatin1String("scene"))
        return;
    const QString groupId = HueJson::at(m_store->resource(sceneId), {QStringLiteral("group"), QStringLiteral("rid")}).toString();
    command(sceneId, object(QStringLiteral("actions"), sceneActions(groupId)));
}

void HueClient::deleteScene(const QString &sceneId)
{
    if (m_store->type(sceneId) != QLatin1String("scene"))
        return;
    finishRequest(m_bridge->remove(QStringLiteral("scene"), sceneId), [this, sceneId](const QByteArray &) {
        // Don't wait for the stream to say so.
        m_store->remove(sceneId);
    });
}

void HueClient::setEnabled(const QString &id, bool enabled)
{
    command(id, object(QStringLiteral("enabled"), enabled));
}

void HueClient::allOff()
{
    // bridge_home has a grouped_light covering every light.
    const QStringList homes = m_store->idsOfType(QStringLiteral("bridge_home"));
    const QString all = homes.isEmpty() ? QString() : m_store->groupedLightOf(homes.first());
    if (!all.isEmpty()) {
        setOn(all, false);
        // Its own state doesn't tell the rooms' grouped_lights; the stream will, but late.
        for (const QString &id : m_store->idsOfType(QStringLiteral("grouped_light"))) {
            QJsonObject off = object(QStringLiteral("on"), object(QStringLiteral("on"), false));
            off.insert(QStringLiteral("id"), id);
            m_store->applyUpdate(off);
        }
        return;
    }
    for (const QString &room : m_store->idsOfType(QStringLiteral("room"))) {
        const QString groupedLight = m_store->groupedLightOf(room);
        if (!groupedLight.isEmpty())
            setOn(groupedLight, false);
    }
}

QColor HueClient::mirekColor(int mirek) const
{
    return HueColor::mirekToRgb(mirek);
}

void HueClient::setLastError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void HueClient::setPairing(PairingState state, int secondsLeft, const QString &error)
{
    m_pairingState = state;
    m_pairingSecondsLeft = secondsLeft;
    m_pairingError = error;
    emit pairingChanged();
}
