include(../common.pri)

TARGET = harbour-lampje
CONFIG += sailfishapp

include(../core/core.pri)

SOURCES += src/main.cpp

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

DISTFILES += \
    qml/icons/light-bulb.svg \
    qml/icons/light-ceiling.svg \
    qml/icons/light-floor.svg \
    qml/icons/light-pendant.svg \
    qml/icons/light-spot.svg \
    qml/icons/light-strip.svg \
    qml/icons/light-table.svg \
    harbour-lampje.desktop \
    qml/harbour-lampje.qml \
    qml/components/ColorWheel.qml \
    qml/components/GroupDelegate.qml \
    qml/components/LevelSlider.qml \
    qml/components/LightDelegate.qml \
    qml/components/LightIcon.qml \
    qml/cover/CoverPage.qml \
    qml/pages/LightPage.qml \
    qml/pages/PairPage.qml \
    qml/pages/RoomPage.qml \
    qml/pages/RoomsPage.qml \
    qml/pages/SensorsPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/SetupPage.qml
