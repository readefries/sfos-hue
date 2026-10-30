TEMPLATE = subdirs

SUBDIRS = core tests
tests.depends = core

# The Silica UI.
packagesExist(sailfishapp) {
    SUBDIRS += app
    app.depends = core
}

OTHER_FILES += rpm/harbour-lampje.spec README.md tools/fakebridge.py

license.files = LICENSE
license.path = /usr/share/licenses/harbour-lampje
INSTALLS += license
