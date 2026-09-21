TEMPLATE = app
TARGET = liveprog_reload_lock_test
CONFIG += console sanitizer sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += JDSP_TEST_HOOKS
QMAKE_CFLAGS += -std=c11 -D_DEFAULT_SOURCE

INCLUDEPATH += $$PWD/../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
LIBS += -L$$OUT_PWD/../ -llibjamesdsp -lm -ldl -lpthread
PRE_TARGETDEPS += $$OUT_PWD/../liblibjamesdsp.a

SOURCES += liveprog_reload_lock_test.c
