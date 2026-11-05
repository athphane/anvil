QT += core gui qml quick quickcontrols2 network testlib dbus
CONFIG += c++17 testcase
TARGET = anvil_tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += anvil_tests.h ../src/backend.h ../src/theme.h ../src/translator.h ../src/sshconnection.h
SOURCES += anvil_tests.cpp ../src/backend.cpp ../src/theme.cpp ../src/translator.cpp ../src/sshconnection.cpp
RESOURCES += ../src/resources.qrc
