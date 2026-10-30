// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUESSESTREAM_H
#define HUESSESTREAM_H

#include <QByteArray>
#include <QList>

// Incremental parser for text/event-stream (Server-Sent Events), as served by the bridge's
// /eventstream/clip/v2. Bytes go in as they arrive; complete events come out.
class HueSseParser
{
public:
    struct Event {
        QByteArray id;
        QByteArray event;
        QByteArray data;
    };

    QList<Event> feed(const QByteArray &bytes);
    void reset();

private:
    void handleLine(QByteArray line, QList<Event> *out);

    QByteArray m_buffer;
    Event m_current;
    bool m_hasData = false;
};

#endif // HUESSESTREAM_H
