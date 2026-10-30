include(../common.pri)

TEMPLATE = lib
TARGET = lampjecore
CONFIG += staticlib
QT = core network gui

HEADERS += \
    huebridge.h \
    hueclient.h \
    huecolor.h \
    huecommandqueue.h \
    huediscovery.h \
    huejson.h \
    huelistmodel.h \
    huemdns.h \
    huemodels.h \
    huepairing.h \
    hueresourcestore.h \
    huessestream.h \
    huetls.h

SOURCES += \
    huebridge.cpp \
    hueclient.cpp \
    huecolor.cpp \
    huecommandqueue.cpp \
    huediscovery.cpp \
    huejson.cpp \
    huelistmodel.cpp \
    huemdns.cpp \
    huemodels.cpp \
    huepairing.cpp \
    hueresourcestore.cpp \
    huessestream.cpp \
    huetls.cpp
