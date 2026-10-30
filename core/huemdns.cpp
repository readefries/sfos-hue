// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huemdns.h"

#include <QStringList>

namespace HueMdns {

namespace {

const char kService[] = "_hue._tcp.local";

quint16 read16(const QByteArray &data, int offset)
{
    return quint16((quint8(data.at(offset)) << 8) | quint8(data.at(offset + 1)));
}

void write16(QByteArray *data, quint16 value)
{
    data->append(char(value >> 8));
    data->append(char(value & 0xff));
}

// Reads a possibly compressed name at *offset and moves *offset past it.
bool readName(const QByteArray &data, int *offset, QString *name)
{
    QStringList labels;
    int pos = *offset;
    int jumps = 0;
    bool jumped = false;
    for (;;) {
        if (pos >= data.size())
            return false;
        const quint8 len = quint8(data.at(pos));
        if (len == 0) {
            ++pos;
            break;
        }
        if ((len & 0xc0) == 0xc0) {
            if (pos + 1 >= data.size() || ++jumps > 32)
                return false;
            if (!jumped)
                *offset = pos + 2;
            jumped = true;
            pos = read16(data, pos) & 0x3fff;
            continue;
        }
        if (pos + 1 + len > data.size())
            return false;
        labels.append(QString::fromUtf8(data.mid(pos + 1, len)));
        pos += 1 + len;
    }
    if (!jumped)
        *offset = pos;
    *name = labels.join(QLatin1Char('.'));
    return true;
}

bool isHueName(const QString &name)
{
    return name.endsWith(QLatin1String(kService), Qt::CaseInsensitive);
}

} // namespace

QByteArray buildQuery(quint16 id)
{
    QByteArray query;
    write16(&query, id);
    write16(&query, 0); // flags: standard query
    write16(&query, 1); // one question
    write16(&query, 0);
    write16(&query, 0);
    write16(&query, 0);
    for (const QByteArray &label : QByteArray(kService).split('.')) {
        query.append(char(label.size()));
        query.append(label);
    }
    query.append('\0');
    write16(&query, PTR);
    write16(&query, 0x8001); // class IN, asking for a unicast answer
    return query;
}

bool parse(const QByteArray &packet, QList<Record> *records)
{
    if (packet.size() < 12)
        return false;
    const int questions = read16(packet, 4);
    const int total = read16(packet, 6) + read16(packet, 8) + read16(packet, 10);
    int offset = 12;
    for (int i = 0; i < questions; ++i) {
        QString name;
        if (!readName(packet, &offset, &name) || offset + 4 > packet.size())
            return false;
        offset += 4;
    }
    for (int i = 0; i < total; ++i) {
        Record record;
        if (!readName(packet, &offset, &record.name) || offset + 10 > packet.size())
            return false;
        record.type = read16(packet, offset);
        const int length = read16(packet, offset + 8);
        offset += 10;
        const int end = offset + length;
        if (end > packet.size())
            return false;
        int rdata = offset;
        switch (record.type) {
        case A:
            if (length == 4) {
                const quint32 ip = (quint32(quint8(packet.at(rdata))) << 24)
                        | (quint32(quint8(packet.at(rdata + 1))) << 16)
                        | (quint32(quint8(packet.at(rdata + 2))) << 8)
                        | quint32(quint8(packet.at(rdata + 3)));
                record.address = QHostAddress(ip);
            }
            break;
        case PTR:
            if (!readName(packet, &rdata, &record.target))
                return false;
            break;
        case SRV:
            if (length < 7)
                return false;
            record.port = read16(packet, rdata + 4);
            rdata += 6;
            if (!readName(packet, &rdata, &record.target))
                return false;
            break;
        case TXT:
            while (rdata < end) {
                const int len = quint8(packet.at(rdata));
                if (rdata + 1 + len > end)
                    return false;
                record.txt.append(packet.mid(rdata + 1, len));
                rdata += 1 + len;
            }
            break;
        default:
            break;
        }
        records->append(record);
        offset = end;
    }
    return true;
}

bool findBridge(const QList<Record> &records, Bridge *bridge)
{
    bool isHue = false;
    QString host;
    for (const Record &record : records) {
        if (record.type == PTR && (isHueName(record.name) || isHueName(record.target)))
            isHue = true;
        if (record.type == SRV && isHueName(record.name)) {
            isHue = true;
            host = record.target;
            bridge->port = record.port;
        }
        if (record.type == TXT && isHueName(record.name)) {
            isHue = true;
            for (const QByteArray &entry : record.txt) {
                if (entry.toLower().startsWith("bridgeid="))
                    bridge->id = QString::fromLatin1(entry.mid(9)).toLower();
            }
        }
    }
    if (!isHue)
        return false;
    // Prefer the address of the SRV target, but take any A record over none.
    for (const Record &record : records) {
        if (record.type != A || record.address.isNull())
            continue;
        if (bridge->address.isNull() || record.name.compare(host, Qt::CaseInsensitive) == 0)
            bridge->address = record.address;
    }
    return true;
}

} // namespace HueMdns
