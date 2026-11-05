QT += core gui qml quick quickcontrols2 network dbus
CONFIG += c++17 release
TARGET = anvil
TEMPLATE = app
HEADERS += src/backend.h src/theme.h src/translator.h src/sshconnection.h
SOURCES += src/main.cpp src/backend.cpp src/theme.cpp src/translator.cpp src/sshconnection.cpp
RESOURCES += src/resources.qrc
