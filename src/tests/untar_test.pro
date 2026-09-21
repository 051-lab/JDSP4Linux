TEMPLATE = app
TARGET = untar_test
CONFIG += console sanitizer sanitize_address sanitize_undefined
CONFIG -= app_bundle

QT += core

INCLUDEPATH += $$PWD/../subprojects/AutoEqIntegration

LIBS += -larchive

SOURCES += untar_test.cpp
