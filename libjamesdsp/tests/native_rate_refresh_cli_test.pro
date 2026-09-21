TEMPLATE = app
TARGET = native_rate_refresh_cli_test
CONFIG += console sanitizer sanitize_address sanitize_undefined
CONFIG -= app_bundle

QT += core

INCLUDEPATH += $$PWD/../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
LIBS += -L$$OUT_PWD/../ -llibjamesdsp -lm -ldl -lpthread
PRE_TARGETDEPS += $$OUT_PWD/../liblibjamesdsp.a

SOURCES += native_rate_refresh_cli_test.c
