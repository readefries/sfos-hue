# Include from projects that link against the static lampjecore library.
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
LAMPJECORE_BUILD_DIR = $$shadowed($$PWD)
LIBS += -L$$LAMPJECORE_BUILD_DIR -llampjecore
PRE_TARGETDEPS += $$LAMPJECORE_BUILD_DIR/liblampjecore.a
QT += network
