TEMPLATE = app
TARGET = uninstallermaker
INCLUDEPATH += . .. ../common

include(../../installerfw.pri)

QT -= gui
QT += qml xml

CONFIG += console
DESTDIR = $$IFW_APP_PATH

SOURCES += uninstallermaker.cpp
