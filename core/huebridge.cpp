// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huebridge.h"

#include "hueresourcestore.h"
#include "huetls.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

int httpStatus(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

} // namespace

HueBridge::HueBridge(QNetworkAccessManager *nam, HueResourceStore *store, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
    , m_store(store)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &HueBridge::start);

    // Nothing from the bridge for this long while connecting or loading: give up and retry.
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(15 * 1000);
    connect(&m_watchdog, &QTimer::timeout, this, [this]() {
        fail(tr("The bridge did not respond"));
    });
}

HueBridge::~HueBridge()
{
    teardown();
}

void HueBridge::setConfig(const HueBridgeConfig &config)
{
    m_config = config;
}

void HueBridge::setReconnectDelays(int initialMsecs, int maxMsecs)
{
    m_initialDelay = initialMsecs;
    m_maxDelay = maxMsecs;
    m_reconnectDelay = initialMsecs;
}

void HueBridge::setTimeout(int msecs)
{
    m_watchdog.setInterval(msecs);
}

QNetworkRequest HueBridge::request(const QString &path) const
{
    QUrl url = m_config.baseUrl;
    url.setPath(path);
    QNetworkRequest req(url);
    req.setRawHeader("hue-application-key", m_config.appKey.toUtf8());
    return req;
}

void HueBridge::start()
{
    if (!m_config.isValid())
        return;
    teardown();
    setState(Connecting);
    openStream();
}

void HueBridge::stop()
{
    teardown();
    m_reconnectDelay = m_initialDelay;
    setState(Idle);
}

void HueBridge::reconnectNow()
{
    m_reconnectDelay = m_initialDelay;
    start();
}

void HueBridge::reload()
{
    if (m_state == Connected && !m_loading)
        load();
}

void HueBridge::openStream()
{
    QNetworkRequest req = request(QStringLiteral("/eventstream/clip/v2"));
    req.setRawHeader("Accept", "text/event-stream");
    m_parser.reset();
    m_certMismatch = false;
    m_stream = m_nam->get(req);
    HueTls::attach(m_stream, m_config.certSha256, nullptr, [this]() { m_certMismatch = true; });
    connect(m_stream.data(), &QNetworkReply::readyRead, this, &HueBridge::onStreamReadyRead);
    connect(m_stream.data(), &QNetworkReply::finished, this, &HueBridge::onStreamFinished);
    // The bridge says ": hi" as soon as the stream is open; load once it has.
    connect(m_stream.data(), &QNetworkReply::metaDataChanged, this, [this]() {
        if (m_stream && httpStatus(m_stream) == 200 && m_state == Connecting && !m_loading)
            load();
    });
    m_watchdog.start();
}

void HueBridge::onStreamReadyRead()
{
    if (!m_stream || httpStatus(m_stream) != 200)
        return;
    if (m_state == Connecting && !m_loading)
        load();
    const QList<HueSseParser::Event> events = m_parser.feed(m_stream->readAll());
    for (const HueSseParser::Event &event : events) {
        const QJsonDocument doc = QJsonDocument::fromJson(event.data);
        if (!doc.isArray())
            continue;
        if (m_loading)
            m_heldEvents.append(doc.array());
        else
            m_store->applyEvents(doc.array());
    }
}

void HueBridge::onStreamFinished()
{
    QNetworkReply *reply = m_stream;
    if (!reply || sender() != reply)
        return;
    m_stream = nullptr;
    reply->deleteLater();
    if (handleFatal(reply))
        return;
    if (httpStatus(reply) == 404)
        fail(noV2Error());
    else
        fail(reply->error() == QNetworkReply::NoError ? tr("The bridge closed the connection")
                                                      : reply->errorString());
}

void HueBridge::load()
{
    m_loading = true;
    m_heldEvents.clear();
    if (m_load)
        m_load->abort();
    m_load = m_nam->get(request(QStringLiteral("/clip/v2/resource")));
    HueTls::attach(m_load, m_config.certSha256, nullptr, [this]() { m_certMismatch = true; });
    connect(m_load.data(), &QNetworkReply::finished, this, &HueBridge::onLoaded);
    m_watchdog.start();
}

void HueBridge::onLoaded()
{
    QNetworkReply *reply = m_load;
    if (!reply || sender() != reply)
        return;
    m_load = nullptr;
    reply->deleteLater();
    m_loading = false;
    m_watchdog.stop();

    if (handleFatal(reply))
        return;
    if (reply->error() != QNetworkReply::NoError) {
        fail(httpStatus(reply) == 404 ? noV2Error() : reply->errorString());
        return;
    }
    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    if (!body.value(QStringLiteral("data")).isArray()) {
        fail(tr("Unexpected answer from the bridge"));
        return;
    }
    m_store->reset(body.value(QStringLiteral("data")).toArray());
    for (const QJsonArray &events : m_heldEvents)
        m_store->applyEvents(events);
    m_heldEvents.clear();
    m_reconnectDelay = m_initialDelay;
    setState(Connected);
}

bool HueBridge::handleFatal(QNetworkReply *reply)
{
    const int status = httpStatus(reply);
    if (status == 401 || status == 403) {
        teardown();
        setState(Unauthorized);
        emit errorOccurred(tr("The bridge no longer accepts this app; pair it again"));
        return true;
    }
    if (m_certMismatch) {
        teardown();
        setState(Untrusted);
        emit errorOccurred(tr("The bridge's certificate changed; pair it again if you trust it"));
        return true;
    }
    return false;
}

QString HueBridge::noV2Error()
{
    return tr("This bridge's software is too old for this app. Update it in the Hue app "
              "(Settings, Software update)");
}

void HueBridge::fail(const QString &error)
{
    teardown();
    emit errorOccurred(error);
    scheduleReconnect();
}

void HueBridge::scheduleReconnect()
{
    setState(WaitingToReconnect);
    m_reconnectTimer.start(m_reconnectDelay);
    m_reconnectDelay = qMin(m_reconnectDelay * 2, m_maxDelay);
}

void HueBridge::teardown()
{
    m_watchdog.stop();
    m_reconnectTimer.stop();
    m_loading = false;
    m_heldEvents.clear();
    // Clear the pointers first: abort() emits finished, whose handlers must ignore it.
    QNetworkReply *stream = m_stream;
    QNetworkReply *load = m_load;
    m_stream = nullptr;
    m_load = nullptr;
    for (QNetworkReply *reply : {stream, load}) {
        if (!reply)
            continue;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void HueBridge::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

QNetworkReply *HueBridge::put(const QString &type, const QString &id, const QJsonObject &body)
{
    QNetworkRequest req = request(QStringLiteral("/clip/v2/resource/%1/%2").arg(type, id));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_nam->put(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    HueTls::attach(reply, m_config.certSha256);
    return reply;
}

QNetworkReply *HueBridge::post(const QString &type, const QJsonObject &body)
{
    QNetworkRequest req = request(QStringLiteral("/clip/v2/resource/%1").arg(type));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    HueTls::attach(reply, m_config.certSha256);
    return reply;
}

QNetworkReply *HueBridge::remove(const QString &type, const QString &id)
{
    QNetworkReply *reply = m_nam->deleteResource(request(QStringLiteral("/clip/v2/resource/%1/%2").arg(type, id)));
    HueTls::attach(reply, m_config.certSha256);
    return reply;
}

QString HueBridge::createdId(const QByteArray &body)
{
    const QJsonArray data = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
    return data.at(0).toObject().value(QStringLiteral("rid")).toString();
}

QString HueBridge::errorOf(QNetworkReply *reply)
{
    return errorOf(reply, reply->readAll());
}

QString HueBridge::errorOf(QNetworkReply *reply, const QByteArray &data)
{
    const QJsonObject body = QJsonDocument::fromJson(data).object();
    const QJsonArray errors = body.value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty())
        return errors.at(0).toObject().value(QStringLiteral("description")).toString();
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    return QString();
}
