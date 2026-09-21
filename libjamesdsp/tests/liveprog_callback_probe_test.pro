TEMPLATE = app
TARGET = liveprog_callback_probe_test
CONFIG += console c11
CONFIG -= app_bundle

INCLUDEPATH += ../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
SOURCES += liveprog_callback_probe_test.c
LIBS += -L../ -llibjamesdsp -lm -ldl -lpthread

QMAKE_CFLAGS += -fno-omit-frame-pointer -fsanitize=address,undefined
QMAKE_LFLAGS += -fsanitize=address,undefined \
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
