TEMPLATE = app
TARGET = liveprog_reload_lock_tsan_test
CONFIG += console c11
CONFIG -= app_bundle
DEFINES += JDSP_TEST_HOOKS

INCLUDEPATH += $$PWD/../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
LIBS += -L$$OUT_PWD/../ -llibjamesdsp -lm -ldl -lpthread
PRE_TARGETDEPS += $$OUT_PWD/../liblibjamesdsp.a

SOURCES += liveprog_reload_lock_test.c
