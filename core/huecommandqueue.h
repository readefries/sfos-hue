// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUECOMMANDQUEUE_H
#define HUECOMMANDQUEUE_H

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTimer>

class HueBridge;
class QNetworkReply;

// Paces commands to what the bridge can take: about ten light commands a second, and one a
// second per group. Commands for the same resource that are still waiting are merged, so a
// dragged slider sends its latest value instead of a backlog of old ones.
class HueCommandQueue : public QObject
{
    Q_OBJECT
public:
    explicit HueCommandQueue(HueBridge *bridge, QObject *parent = nullptr);

    void setIntervals(int lightMsecs, int groupMsecs);
    void enqueue(const QString &type, const QString &id, const QJsonObject &body);
    void clear();
    int pendingCount() const { return m_pending.size(); }
    bool isIdle() const { return m_pending.isEmpty() && !m_inFlight; }

signals:
    // A command the bridge rejected (or that never got there).
    void commandFailed(const QString &type, const QString &id, const QString &error);
    void idle();

private:
    struct Command {
        QString type;
        QString id;
        QJsonObject body;
    };

    void sendNext();
    void onFinished();
    int indexOfReady(qint64 now, qint64 *waitMsecs) const;

    HueBridge *m_bridge;
    QList<Command> m_pending;
    QNetworkReply *m_inFlight = nullptr;
    Command m_inFlightCommand;
    QTimer m_timer;
    QTimer m_requestTimeout;
    QElapsedTimer m_clock;
    qint64 m_lastSend = -1;
    QHash<QString, qint64> m_lastGroupSend;
    int m_lightInterval = 100;
    int m_groupInterval = 1000;
};

#endif // HUECOMMANDQUEUE_H
