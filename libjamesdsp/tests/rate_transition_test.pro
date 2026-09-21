QT += core
CONFIG += console c11
contains(CONFIG, DEBUG_TSAN) {
    QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer
    QMAKE_LFLAGS += -fsanitize=thread
} else {
    CONFIG += sanitizer sanitize_address sanitize_undefined
}
TEMPLATE = app
TARGET = rate_transition_test
INCLUDEPATH += .. ../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
SOURCES += rate_transition_test.c
LIBS += -L../ -llibjamesdsp -lm -ldl
