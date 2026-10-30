include(../common.pri)

TARGET = harbour-lampje
CONFIG += sailfishapp

include(../core/core.pri)

SOURCES += src/main.cpp

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

DISTFILES += \
    harbour-lampje.desktop \
    qml/harbour-lampje.qml \
    qml/components/ColorWheel.qml \
    qml/components/GroupDelegate.qml \
    qml/components/LevelSlider.qml \
    qml/components/LightDelegate.qml \
    qml/cover/CoverPage.qml \
    qml/pages/LightPage.qml \
    qml/pages/PairPage.qml \
    qml/pages/RoomPage.qml \
    qml/pages/RoomsPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/SetupPage.qml
