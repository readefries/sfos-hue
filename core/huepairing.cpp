// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huepairing.h"

#include "huetls.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
// "link button not pressed"
const int kLinkButtonError = 101;
}

HuePairing::HuePairing(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
    m_pollTimer.setSingleShot(true);
    m_pollTimer.setInterval(2000);
    connect(&m_pollTimer, &QTimer::timeout, this, &HuePairing::poll);
}

void HuePairing::start(const QUrl &baseUrl, const QString &deviceType)
{
    cancel();
    m_baseUrl = baseUrl;
    m_deviceType = deviceType.left(40); // the bridge's limit
    m_certSha256.clear();
    m_running = true;
    m_elapsed.start();
    poll();
}

void HuePairing::cancel()
{
    m_running = false;
    m_pollTimer.stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void HuePairing::poll()
{
    QUrl url = m_baseUrl;
    url.setPath(QStringLiteral("/api"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    body.insert(QStringLiteral("devicetype"), m_deviceType);
    // No generatekey: the client key it adds is only for Entertainment streaming, which we don't
    // do, and firmware older than API 1.22 refuses the whole request over it.
    m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    HueTls::attach(m_reply, QByteArray(), [this](const QByteArray &sha256) { m_certSha256 = sha256; });
    connect(m_reply.data(), &QNetworkReply::finished, this, &HuePairing::onReply);
}

void HuePairing::onReply()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_running = false;
        emit failed(reply->errorString());
        return;
    }
    const QJsonObject result = QJsonDocument::fromJson(reply->readAll()).array().at(0).toObject();
    const QJsonObject success = result.value(QStringLiteral("success")).toObject();
    const QJsonObject error = result.value(QStringLiteral("error")).toObject();
    const QString key = success.value(QStringLiteral("username")).toString();

    if (!key.isEmpty()) {
        m_running = false;
        emit paired(key, m_certSha256);
    } else if (error.value(QStringLiteral("type")).toInt() == kLinkButtonError) {
        const qint64 left = m_timeout - m_elapsed.elapsed();
        if (left <= 0) {
            m_running = false;
            emit failed(tr("The link button wasn't pressed in time"));
            return;
        }
        emit waitingForButton(int((left + 999) / 1000));
        m_pollTimer.start();
    } else {
        m_running = false;
        const QString description = error.value(QStringLiteral("description")).toString();
        emit failed(description.isEmpty() ? tr("Unexpected answer from the bridge") : description);
    }
}
