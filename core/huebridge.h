// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUEBRIDGE_H
#define HUEBRIDGE_H

#include "huessestream.h"

#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class HueResourceStore;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

struct HueBridgeConfig {
    QUrl baseUrl;           // https://<address>, or http://... for the fake bridge
    QString appKey;         // from pairing
    QByteArray certSha256;  // pinned on pairing; empty for plain http

    bool isValid() const { return baseUrl.isValid() && !appKey.isEmpty(); }
};

// Live connection to a paired bridge over CLIP v2.
//
// Opens /eventstream/clip/v2, then loads every resource into the store and applies the stream's
// events to it from then on (events that arrive while loading are held back and applied after).
// When the stream ends, it reconnects with exponential backoff and loads everything again, so
// nothing missed in between goes unnoticed.
class HueBridge : public QObject
{
    Q_OBJECT
public:
    enum State {
        Idle,
        Connecting,
        Connected,
        WaitingToReconnect,
        Unauthorized,   // the app key was revoked or never valid; pair again
        Untrusted       // the bridge presented another certificate than the pinned one
    };
    Q_ENUM(State)

    HueBridge(QNetworkAccessManager *nam, HueResourceStore *store, QObject *parent = nullptr);
    ~HueBridge() override;

    void setConfig(const HueBridgeConfig &config);
    HueBridgeConfig config() const { return m_config; }
    State state() const { return m_state; }

    void setReconnectDelays(int initialMsecs, int maxMsecs);
    void setTimeout(int msecs);

    // PUT /clip/v2/resource/<type>/<id>. The caller owns the reply; errorOf() interprets it.
    QNetworkReply *put(const QString &type, const QString &id, const QJsonObject &body);
    // POST /clip/v2/resource/<type>, creating one; createdId() tells its id.
    QNetworkReply *post(const QString &type, const QJsonObject &body);
    // DELETE /clip/v2/resource/<type>/<id>.
    QNetworkReply *remove(const QString &type, const QString &id);
    static QString createdId(const QByteArray &body);
    // Empty if the request succeeded, otherwise what went wrong.
    static QString errorOf(QNetworkReply *reply);
    static QString errorOf(QNetworkReply *reply, const QByteArray &body);

public slots:
    void start();
    void stop();
    // Drop the connection and connect again right away, e.g. after the phone slept.
    void reconnectNow();
    // Load all resources again without touching the stream, e.g. after a failed command left
    // optimistic state behind in the store.
    void reload();

signals:
    void stateChanged(HueBridge::State state);
    void errorOccurred(const QString &error);

private:
    QNetworkRequest request(const QString &path) const;
    void openStream();
    void onStreamReadyRead();
    void onStreamFinished();
    void load();
    void onLoaded();
    bool handleFatal(QNetworkReply *reply);
    void fail(const QString &error);
    static QString noV2Error();
    void scheduleReconnect();
    void teardown();
    void setState(State state);

    QNetworkAccessManager *m_nam;
    HueResourceStore *m_store;
    HueBridgeConfig m_config;
    State m_state = Idle;

    QPointer<QNetworkReply> m_stream;
    QPointer<QNetworkReply> m_load;
    HueSseParser m_parser;
    bool m_loading = false;
    QList<QJsonArray> m_heldEvents;
    bool m_certMismatch = false;

    QTimer m_reconnectTimer;
    QTimer m_watchdog;
    int m_initialDelay = 1000;
    int m_maxDelay = 60 * 1000;
    int m_reconnectDelay = 1000;
};

#endif // HUEBRIDGE_H
