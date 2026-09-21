TEMPLATE = app
TARGET = liveprog_runtime_test
CONFIG += console
CONFIG -= app_bundle
CONFIG += sanitizer sanitize_address sanitize_undefined
DEFINES += JDSP_TEST_HOOKS

QT += core

INCLUDEPATH += $$PWD/../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
LIBS += -L$$OUT_PWD/../ -llibjamesdsp -lm -ldl -lpthread

SOURCES += liveprog_runtime_test.c
