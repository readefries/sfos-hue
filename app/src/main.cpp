// SPDX-License-Identifier: AGPL-3.0-or-later
//
// harbour-lampje: the Silica UI on top of HueClient, which owns the connection to the bridge.

#include "hueclient.h"
#include "huediscovery.h"
#include "huemodels.h"
#include "hueresourcestore.h"

#include <sailfishapp.h>

#include <QGuiApplication>
#include <QLocale>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // Must match OrganizationName and ApplicationName in harbour-lampje.desktop.
    app->setOrganizationName(QStringLiteral("nl.readefries"));
    app->setApplicationName(QStringLiteral("harbour-lampje"));
    app->setApplicationVersion(QStringLiteral(LAMPJE_VERSION));

    // English underneath for plural forms, which the source can't give, and the phone's language on
    // top. Translators installed last are asked first.
    const QString translations = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    QTranslator *english = new QTranslator(app.data());
    if (english->load(QStringLiteral("harbour-lampje-en"), translations))
        app->installTranslator(english);
    QTranslator *local = new QTranslator(app.data());
    if (local->load(QLocale(), QStringLiteral("harbour-lampje"), QStringLiteral("-"), translations))
        app->installTranslator(local);

    const char *uri = "harbour.lampje";
    const QString global = QStringLiteral("use the global \"hue\"");
    qmlRegisterUncreatableType<HueClient>(uri, 1, 0, "HueClient", global);
    qmlRegisterUncreatableType<HueResourceStore>(uri, 1, 0, "ResourceStore", global);
    qmlRegisterUncreatableType<HueDiscovery>(uri, 1, 0, "Discovery", global);
    qmlRegisterType<HueRoomModel>(uri, 1, 0, "RoomModel");
    qmlRegisterType<HueLightModel>(uri, 1, 0, "LightModel");
    qmlRegisterType<HueSceneModel>(uri, 1, 0, "SceneModel");
    qmlRegisterType<HueSensorModel>(uri, 1, 0, "SensorModel");

    HueClient hue;
    QObject::connect(app.data(), &QGuiApplication::applicationStateChanged, &hue,
                     [&hue](Qt::ApplicationState state) {
        hue.setApplicationActive(state == Qt::ApplicationActive);
    });

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("hue"), &hue);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}
