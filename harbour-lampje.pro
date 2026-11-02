TEMPLATE = subdirs

SUBDIRS = core tests
tests.depends = core

packagesExist(sailfishapp) {
    # The Silica UI, for the phone.
    SUBDIRS += app
    app.depends = core
} else:greaterThan(QT_MAJOR_VERSION, 5):qtHaveModule(quickcontrols2) {
    # The Kirigami UI, for the desktop (see flatpak/).
    SUBDIRS += desktop
    desktop.depends = core
    isEmpty(PREFIX): PREFIX = /usr/local
    license.path = $$PREFIX/share/licenses/nl.readefries.Lampje
}

OTHER_FILES += rpm/harbour-lampje.spec README.md tools/fakebridge.py flatpak/nl.readefries.Lampje.yml

license.files = LICENSE
isEmpty(license.path): license.path = /usr/share/licenses/harbour-lampje
INSTALLS += license
