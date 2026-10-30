// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUEPAIRING_H
#define HUEPAIRING_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Gets an application key: keeps asking the bridge for one until someone presses its link button,
// or time runs out. Pins the bridge's certificate while at it.
class HuePairing : public QObject
{
    Q_OBJECT
public:
    explicit HuePairing(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void setPollInterval(int msecs) { m_pollTimer.setInterval(msecs); }
    void setTimeout(int msecs) { m_timeout = msecs; }
    bool isRunning() const { return m_running; }

public slots:
    void start(const QUrl &baseUrl, const QString &deviceType);
    void cancel();

signals:
    void waitingForButton(int secondsLeft);
    void paired(const QString &appKey, const QByteArray &certSha256);
    void failed(const QString &error);

private:
    void poll();
    void onReply();

    QNetworkAccessManager *m_nam;
    QUrl m_baseUrl;
    QString m_deviceType;
    QByteArray m_certSha256;
    QPointer<QNetworkReply> m_reply;
    QTimer m_pollTimer;
    QElapsedTimer m_elapsed;
    int m_timeout = 60 * 1000;
    bool m_running = false;
};

#endif // HUEPAIRING_H
