// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUEDISCOVERY_H
#define HUEDISCOVERY_H

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class QNetworkAccessManager;
class QUdpSocket;

// Finds bridges on the local network with mDNS and Philips' discovery service, and asks each one
// found (or typed in) for its name and id. Bridges show up in `bridges` as they are confirmed.
class HueDiscovery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    // [{id, host, name, modelId, supported}], sorted by name.
    Q_PROPERTY(QVariantList bridges READ bridges NOTIFY bridgesChanged)
public:
    explicit HueDiscovery(QNetworkAccessManager *nam, QObject *parent = nullptr);

    // https://<host>, unless host already names a scheme (http://... for the fake bridge).
    static QUrl baseUrlFor(const QString &host);

    void setCloudUrl(const QUrl &url) { m_cloudUrl = url; }
    void setMdnsEnabled(bool enabled) { m_mdnsEnabled = enabled; }
    void setDuration(int msecs) { m_stopTimer.setInterval(msecs); }

    bool isRunning() const { return m_running; }
    QVariantList bridges() const { return m_bridges; }

public slots:
    void start();
    void stop();
    // Ask the bridge at host (an address, optionally with a port or scheme) who it is.
    void probe(const QString &host);

signals:
    void runningChanged();
    void bridgesChanged();
    void probeFinished(const QString &host, bool ok, const QString &error);

private:
    void sendMdnsQuery();
    void readMdns();
    void queryCloud();
    void addBridge(const QVariantMap &bridge);
    void setRunning(bool running);

    QNetworkAccessManager *m_nam;
    QUdpSocket *m_socket = nullptr;
    QUrl m_cloudUrl;
    bool m_mdnsEnabled = true;
    bool m_running = false;
    int m_queriesSent = 0;
    QTimer m_queryTimer;
    QTimer m_stopTimer;
    QSet<QString> m_probed;
    int m_probesRunning = 0;
    QVariantList m_bridges;
};

#endif // HUEDISCOVERY_H
