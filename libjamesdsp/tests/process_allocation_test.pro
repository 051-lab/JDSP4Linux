QT += core
CONFIG += console c11
DEFINES += JDSP_TEST_HOOKS
TEMPLATE = app
TARGET = process_allocation_test
INCLUDEPATH += .. ../subtree/Main/libjamesdsp/jni/jamesdsp/jdsp
SOURCES += process_allocation_test.c
LIBS += -L../ -llibjamesdsp -lm -ldl -lpthread
QMAKE_LFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=pthread_mutex_lock
QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
