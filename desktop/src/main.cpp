// SPDX-License-Identifier: AGPL-3.0-or-later
//
// lampje: the desktop UI (Kirigami) on top of HueClient, as the phone's harbour-lampje is.

#include "desktopsettings.h"
#include "hueclient.h"
#include "huediscovery.h"
#include "huemodels.h"
#include "hueresourcestore.h"

#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStyleHints>
#include <QSysInfo>
#include <QTranslator>

#ifdef LAMPJE_HAVE_KCOLORSCHEME
#include <KColorSchemeManager>
#endif

Q_LOGGING_CATEGORY(lcTheme, "lampje.theme", QtWarningMsg)

namespace {

// Breeze's colours come from KDE's colour scheme, which only Plasma sets; elsewhere (Sway, GNOME)
// it stays light whatever the desktop prefers. So pick Breeze Dark or Light by the preference
// Qt reads from the desktop portal (org.freedesktop.appearance color-scheme), and follow it when
// it changes. On Plasma, its own scheme stays.
void followColorScheme(QGuiApplication *app)
{
#ifdef LAMPJE_HAVE_KCOLORSCHEME
    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"), Qt::CaseInsensitive))
        return;
    KColorSchemeManager *manager = KColorSchemeManager::instance();
    manager->setAutosaveChanges(false);
    auto apply = [manager](Qt::ColorScheme scheme) {
        qCDebug(lcTheme) << "colour scheme" << scheme;
        if (scheme == Qt::ColorScheme::Dark)
            manager->activateSchemeId(QStringLiteral("BreezeDark"));
        else if (scheme == Qt::ColorScheme::Light)
            manager->activateSchemeId(QStringLiteral("BreezeLight"));
    };
    apply(app->styleHints()->colorScheme());
    QObject::connect(app->styleHints(), &QStyleHints::colorSchemeChanged, manager, apply);
#else
    Q_UNUSED(app)
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Settings go to ~/.config/nl.readefries/lampje/ (in the Flatpak, under ~/.var/app/).
    app.setOrganizationName(QStringLiteral("nl.readefries"));
    app.setApplicationName(QStringLiteral("lampje"));
    app.setApplicationDisplayName(QStringLiteral("Lampje"));
    app.setApplicationVersion(QStringLiteral(LAMPJE_VERSION));
    // The Wayland app id: matches nl.readefries.Lampje.desktop, for its icon and name.
    app.setDesktopFileName(QStringLiteral("nl.readefries.Lampje"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("nl.readefries.Lampje"),
                                       QIcon(QStringLiteral(":/icons/lampje.svg"))));

    // Plasma's style where there is one; Fusion elsewhere (Sway, GNOME without it installed).
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
    followColorScheme(&app);

    // As on the phone: English underneath for plural forms, the user's language on top.
    QTranslator english;
    if (english.load(QStringLiteral(":/i18n/lampje-en")))
        app.installTranslator(&english);
    QTranslator local;
    if (local.load(QLocale(), QStringLiteral("lampje"), QStringLiteral("-"), QStringLiteral(":/i18n")))
        app.installTranslator(&local);

    const char *uri = "harbour.lampje";
    const QString global = QStringLiteral("use the global \"hue\"");
    qmlRegisterUncreatableType<HueClient>(uri, 1, 0, "HueClient", global);
    qmlRegisterUncreatableType<HueResourceStore>(uri, 1, 0, "ResourceStore", global);
    qmlRegisterUncreatableType<HueDiscovery>(uri, 1, 0, "Discovery", global);
    qmlRegisterType<HueRoomModel>(uri, 1, 0, "RoomModel");
    qmlRegisterType<HueLightModel>(uri, 1, 0, "LightModel");
    qmlRegisterType<HueSceneModel>(uri, 1, 0, "SceneModel");
    qmlRegisterType<HueSensorModel>(uri, 1, 0, "SensorModel");
    qmlRegisterType<HueScheduleModel>(uri, 1, 0, "ScheduleModel");

    HueClient hue;
    // The Hue app lists paired apps by this: "lampje#<computer>", next to "lampje#sailfishos".
    hue.setDeviceName(QSysInfo::machineHostName());
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &hue,
                     [&hue](Qt::ApplicationState state) {
        hue.setApplicationActive(state == Qt::ApplicationActive);
    });

    DesktopSettings desktop;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("hue"), &hue);
    engine.rootContext()->setContextProperty(QStringLiteral("desktop"), &desktop);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
