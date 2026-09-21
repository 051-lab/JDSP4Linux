TEMPLATE = app
TARGET = crossfeed_lifecycle_test
CONFIG += console c11
CONFIG -= app_bundle

INCLUDEPATH += ../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
SOURCES += crossfeed_lifecycle_test.c
LIBS += -L../ -llibjamesdsp -lm -ldl -lpthread

QMAKE_CFLAGS += -fno-omit-frame-pointer
contains(CONFIG, DEBUG_TSAN) {
    QMAKE_CFLAGS += -fsanitize=thread
    QMAKE_LFLAGS += -fsanitize=thread
} else {
    QMAKE_CFLAGS += -fsanitize=address,undefined
    QMAKE_LFLAGS += -fsanitize=address,undefined
}
