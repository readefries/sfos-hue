include(../common.pri)

TARGET = harbour-lampje
CONFIG += sailfishapp sailfishapp_i18n

include(../core/core.pri)

SOURCES += src/main.cpp

# lupdate also looks in the core, for its error messages.
TRANSLATION_SOURCES += $$PWD/../core
# English only for plural forms ("1 light", "2 lights"); everything else is in the source.
TRANSLATIONS += \
    translations/harbour-lampje-en.ts \
    translations/harbour-lampje-nl.ts

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

DISTFILES += \
    qml/icons/light-bulb.svg \
    qml/icons/light-ceiling.svg \
    qml/icons/light-floor.svg \
    qml/icons/light-pendant.svg \
    qml/icons/light-spot.svg \
    qml/icons/light-strip.svg \
    qml/icons/light-table.svg \
    qml/icons/room-bath.svg \
    qml/icons/room-bed.svg \
    qml/icons/room-desk.svg \
    qml/icons/room-door.svg \
    qml/icons/room-garden.svg \
    qml/icons/room-home.svg \
    qml/icons/room-kitchen.svg \
    qml/icons/room-sofa.svg \
    qml/icons/power.svg \
    qml/icons/scene.svg \
    harbour-lampje.desktop \
    qml/harbour-lampje.qml \
    qml/components/Choice.qml \
    qml/components/ColorWheel.qml \
    qml/components/KindComboBox.qml \
    qml/components/archetypes.js \
    qml/components/coveractions.js \
    qml/components/icons.js \
    qml/components/GroupDelegate.qml \
    qml/components/LevelSlider.qml \
    qml/components/LightDelegate.qml \
    qml/components/LightIcon.qml \
    qml/components/RoomIcon.qml \
    qml/cover/CoverPage.qml \
    qml/pages/CoverActionPage.qml \
    qml/pages/EditDialog.qml \
    qml/pages/LightPage.qml \
    qml/pages/PairPage.qml \
    qml/pages/RoomPage.qml \
    qml/pages/RoomsPage.qml \
    qml/pages/SceneDialog.qml \
    qml/pages/SensorsPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/SetupPage.qml \
    qml/pages/ZoneDialog.qml
