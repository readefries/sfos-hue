// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huecommandqueue.h"

#include "huebridge.h"
#include "huejson.h"

#include <QNetworkReply>

namespace {
const QString kGroupedLight = QStringLiteral("grouped_light");
}

HueCommandQueue::HueCommandQueue(HueBridge *bridge, QObject *parent)
    : QObject(parent)
    , m_bridge(bridge)
{
    m_clock.start();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &HueCommandQueue::sendNext);
    m_requestTimeout.setSingleShot(true);
    m_requestTimeout.setInterval(10 * 1000);
    connect(&m_requestTimeout, &QTimer::timeout, this, [this]() {
        if (m_inFlight)
            m_inFlight->abort(); // finishes it with an error
    });
}

void HueCommandQueue::setIntervals(int lightMsecs, int groupMsecs)
{
    m_lightInterval = lightMsecs;
    m_groupInterval = groupMsecs;
}

void HueCommandQueue::enqueue(const QString &type, const QString &id, const QJsonObject &body)
{
    bool merged = false;
    for (Command &command : m_pending) {
        if (command.id == id) {
            command.body = HueJson::merged(command.body, body);
            merged = true;
            break;
        }
    }
    if (!merged)
        m_pending.append(Command{type, id, body});
    // Even with the timer running: it may be waiting for another group, while this one can go.
    if (!m_inFlight)
        sendNext();
}

void HueCommandQueue::clear()
{
    m_pending.clear();
    m_timer.stop();
}

int HueCommandQueue::indexOfReady(qint64 now, qint64 *waitMsecs) const
{
    *waitMsecs = -1;
    if (m_lastSend >= 0 && now - m_lastSend < m_lightInterval) {
        *waitMsecs = m_lightInterval - (now - m_lastSend);
        return -1;
    }
    // Oldest first, but a group that has to wait doesn't hold up the lights behind it.
    for (int i = 0; i < m_pending.size(); ++i) {
        const Command &command = m_pending.at(i);
        if (command.type != kGroupedLight)
            return i;
        const qint64 last = m_lastGroupSend.value(command.id, -1);
        if (last < 0 || now - last >= m_groupInterval)
            return i;
        const qint64 wait = m_groupInterval - (now - last);
        if (*waitMsecs < 0 || wait < *waitMsecs)
            *waitMsecs = wait;
    }
    return -1;
}

void HueCommandQueue::sendNext()
{
    if (m_inFlight || m_pending.isEmpty())
        return;
    const qint64 now = m_clock.elapsed();
    qint64 wait = 0;
    const int index = indexOfReady(now, &wait);
    if (index < 0) {
        m_timer.start(int(qMax<qint64>(wait, 1)));
        return;
    }
    m_inFlightCommand = m_pending.takeAt(index);
    m_lastSend = now;
    if (m_inFlightCommand.type == kGroupedLight)
        m_lastGroupSend.insert(m_inFlightCommand.id, now);
    m_inFlight = m_bridge->put(m_inFlightCommand.type, m_inFlightCommand.id, m_inFlightCommand.body);
    connect(m_inFlight, &QNetworkReply::finished, this, &HueCommandQueue::onFinished);
    m_requestTimeout.start();
}

void HueCommandQueue::onFinished()
{
    QNetworkReply *reply = m_inFlight;
    m_inFlight = nullptr;
    m_requestTimeout.stop();
    if (!reply)
        return;
    reply->deleteLater();
    const QString error = HueBridge::errorOf(reply);
    if (!error.isEmpty())
        emit commandFailed(m_inFlightCommand.type, m_inFlightCommand.id, error);
    if (m_pending.isEmpty())
        emit idle();
    else
        sendNext();
}
