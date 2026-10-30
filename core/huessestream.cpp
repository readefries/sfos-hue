// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huessestream.h"

QList<HueSseParser::Event> HueSseParser::feed(const QByteArray &bytes)
{
    QList<Event> out;
    m_buffer += bytes;
    int start = 0;
    for (;;) {
        const int end = m_buffer.indexOf('\n', start);
        if (end < 0)
            break;
        QByteArray line = m_buffer.mid(start, end - start);
        if (line.endsWith('\r'))
            line.chop(1);
        handleLine(line, &out);
        start = end + 1;
    }
    m_buffer.remove(0, start);
    return out;
}

void HueSseParser::reset()
{
    m_buffer.clear();
    m_current = Event();
    m_hasData = false;
}

void HueSseParser::handleLine(QByteArray line, QList<Event> *out)
{
    if (line.isEmpty()) {
        // Blank line: dispatch. Events without data (e.g. only an id) are dropped, per the spec.
        if (m_hasData)
            out->append(m_current);
        m_current = Event();
        m_hasData = false;
        return;
    }
    if (line.startsWith(':'))
        return; // comment, e.g. the bridge's ": hi" on connect

    const int colon = line.indexOf(':');
    const QByteArray field = colon < 0 ? line : line.left(colon);
    QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
    if (value.startsWith(' '))
        value.remove(0, 1);

    if (field == "data") {
        if (m_hasData)
            m_current.data += '\n';
        m_current.data += value;
        m_hasData = true;
    } else if (field == "id") {
        m_current.id = value;
    } else if (field == "event") {
        m_current.event = value;
    }
}
