// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef DESKTOPSETTINGS_H
#define DESKTOPSETTINGS_H

#include <QObject>
#include <QString>

class QDBusVariant;

// The desktop's preferences the locale doesn't carry, from the settings portal (so also inside
// the Flatpak): GNOME's clock format, which Sway and other desktops using GNOME's settings share.
// QML sees it as `desktop`.
class DesktopSettings : public QObject
{
    Q_OBJECT
    // "24h", "12h", or empty to go by the locale.
    Q_PROPERTY(QString clockFormat READ clockFormat NOTIFY clockFormatChanged)
public:
    explicit DesktopSettings(QObject *parent = nullptr);

    QString clockFormat() const { return m_clockFormat; }

signals:
    void clockFormatChanged();

private slots:
    void onSettingChanged(const QString &group, const QString &key, const QDBusVariant &value);

private:
    void setClockFormat(const QString &format);

    QString m_clockFormat;
};

#endif // DESKTOPSETTINGS_H
