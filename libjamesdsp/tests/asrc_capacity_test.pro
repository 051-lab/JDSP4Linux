TEMPLATE = app
TARGET = asrc_capacity_test
CONFIG += console sanitizer sanitize_address sanitize_undefined
CONFIG -= app_bundle

QT += core
DEFINES += JDSP_TEST_HOOKS

INCLUDEPATH += $$PWD/../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
LIBS += -L$$OUT_PWD/../ -llibjamesdsp -lm -ldl -lpthread
PRE_TARGETDEPS += $$OUT_PWD/../liblibjamesdsp.a

SOURCES += asrc_capacity_test.c
