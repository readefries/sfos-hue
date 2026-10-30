include(../../common.pri)
include(../../core/core.pri)

TEMPLATE = app
TARGET = tst_lampje
CONFIG += testcase no_testcase_installs console
CONFIG -= app_bundle
QT = core gui network testlib

DEFINES += FIXTURE_DIR=\\\"$$PWD/../common\\\"
INCLUDEPATH += ../common
HEADERS += ../common/fakehuebridge.h
SOURCES += tst_lampje.cpp
