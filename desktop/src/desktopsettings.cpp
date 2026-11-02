// SPDX-License-Identifier: AGPL-3.0-or-later
#include "desktopsettings.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcDesktop, "lampje.desktop", QtWarningMsg)

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.Settings");
const QString kGroup = QStringLiteral("org.gnome.desktop.interface");
const QString kKey = QStringLiteral("clock-format");

// Read() (older portals) wraps the value in one more variant than ReadOne().
QVariant unwrap(QVariant value)
{
    while (value.canConvert<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}
}

DesktopSettings::DesktopSettings(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    bus.connect(kService, kPath, kInterface, QStringLiteral("SettingChanged"), this,
                SLOT(onSettingChanged(QString,QString,QDBusVariant)));

    QDBusMessage read = QDBusMessage::createMethodCall(kService, kPath, kInterface, QStringLiteral("Read"));
    read << kGroup << kKey;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(read), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QVariant> reply = *call;
        qCDebug(lcDesktop) << "clock-format:" << (reply.isError() ? QVariant(reply.error().message()) : reply.value());
        if (!reply.isError())
            setClockFormat(unwrap(reply.value()).toString());
    });
}

void DesktopSettings::onSettingChanged(const QString &group, const QString &key, const QDBusVariant &value)
{
    if (group == kGroup && key == kKey)
        setClockFormat(unwrap(value.variant()).toString());
}

void DesktopSettings::setClockFormat(const QString &format)
{
    qCDebug(lcDesktop) << "clock format" << format;
    const QString known = format == QLatin1String("24h") || format == QLatin1String("12h") ? format : QString();
    if (m_clockFormat == known)
        return;
    m_clockFormat = known;
    emit clockFormatChanged();
}
