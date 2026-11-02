# Shared settings. Sailfish OS ships Qt 5.6, so stick to APIs available there; the desktop app
# builds the same code with Qt 6.
greaterThan(QT_MAJOR_VERSION, 5): CONFIG += c++17
else: CONFIG += c++11
DEFINES += QT_NO_CAST_FROM_ASCII

isEmpty(VERSION): VERSION = 0.1.0
DEFINES += LAMPJE_VERSION=\\\"$$VERSION\\\"
