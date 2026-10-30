// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUEMDNS_H
#define HUEMDNS_H

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>

// Just enough DNS to find bridges with mDNS: a query for _hue._tcp.local, and reading answers.
namespace HueMdns {

enum RecordType : quint16 {
    A = 1,
    PTR = 12,
    TXT = 16,
    SRV = 33
};

struct Record {
    QString name;
    quint16 type = 0;
    QString target;            // PTR and SRV
    quint16 port = 0;          // SRV
    QHostAddress address;      // A
    QList<QByteArray> txt;     // TXT
};

struct Bridge {
    QString id;                // from TXT bridgeid=, lower case; may be empty
    QHostAddress address;      // null if the answer carried no A record
    quint16 port = 443;
};

QByteArray buildQuery(quint16 id = 0);
// False if the packet is malformed; records parsed before the problem are kept.
bool parse(const QByteArray &packet, QList<Record> *records);
// Whether the records describe a Hue bridge, and if so what they say about it.
bool findBridge(const QList<Record> &records, Bridge *bridge);

} // namespace HueMdns

#endif // HUEMDNS_H
