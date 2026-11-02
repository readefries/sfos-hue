include(../common.pri)

TEMPLATE = app
TARGET = lampje
# SVG icons load through Qt SVG's image plugin at run time; nothing to link.
QT += quick qml quickcontrols2 dbus

include(../core/core.pri)

HEADERS += src/desktopsettings.h
SOURCES += src/main.cpp src/desktopsettings.cpp

# KDE's colour schemes (KF6 ColorScheme, in KDE's Flatpak runtime), to follow the desktop's dark
# mode outside Plasma; without them it looks as the style decides.
# Next to Qt's headers (/usr/include/QtCore, as in the Flatpak SDK) or one up (/usr/include/qt6).
for(dir, $$list($$[QT_INSTALL_HEADERS]/KF6 $$clean_path($$[QT_INSTALL_HEADERS]/../KF6))) {
    isEmpty(KF6_INCLUDES):exists($$dir/KColorScheme/KColorSchemeManager): KF6_INCLUDES = $$dir
}
!isEmpty(KF6_INCLUDES) {
    DEFINES += LAMPJE_HAVE_KCOLORSCHEME
    INCLUDEPATH += $$KF6_INCLUDES/KColorScheme $$KF6_INCLUDES/KConfig $$KF6_INCLUDES/KConfigCore \
        $$KF6_INCLUDES/KConfigGui
    LIBS += -lKF6ColorScheme
}
RESOURCES += lampje.qrc
# The QML compiled into the binary. Otherwise Qt caches it on disk by the files' modification
# times, which Flatpak sets all the same: an update would go on running the old QML.
CONFIG += qtquickcompiler

# Compiled into the binary (:/i18n/lampje-*.qm). lupdate and tools/translations.py fill them,
# as for the phone's.
CONFIG += lrelease embed_translations
QM_FILES_RESOURCE_PREFIX = /i18n
TRANSLATIONS += \
    translations/lampje-en.ts \
    translations/lampje-nl.ts

# Flatpak passes PREFIX=/app.
isEmpty(PREFIX): PREFIX = /usr/local
target.path = $$PREFIX/bin
desktopfile.files = nl.readefries.Lampje.desktop
desktopfile.path = $$PREFIX/share/applications
metainfo.files = nl.readefries.Lampje.metainfo.xml
metainfo.path = $$PREFIX/share/metainfo
icon.files = icons/nl.readefries.Lampje.svg
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
INSTALLS += target desktopfile metainfo icon
