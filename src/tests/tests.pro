TEMPLATE = app
TARGET = config_io_test
CONFIG += console
CONFIG -= app_bundle

QT += core

INCLUDEPATH += $$PWD/.. $$PWD/../audio/base

SOURCES += \
    config_io_test.cpp \
    ../config/ConfigIO.cpp \
    ../utils/Log.cpp
