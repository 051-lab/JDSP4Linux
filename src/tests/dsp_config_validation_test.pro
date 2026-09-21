TEMPLATE = app
TARGET = dsp_config_validation_test
CONFIG += console sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += HEADLESS

QT += core

INCLUDEPATH += $$PWD/.. $$PWD/../audio/base

SOURCES += \
    dsp_config_validation_test.cpp \
    ../config/AppConfig.cpp \
    ../config/ConfigContainer.cpp \
    ../config/ConfigIO.cpp \
    ../utils/Log.cpp

HEADERS += ../config/AppConfig.h ../config/DspConfig.h

RESOURCES += $$PWD/../../resources/resources.qrc
