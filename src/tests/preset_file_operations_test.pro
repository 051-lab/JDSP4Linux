TEMPLATE = app
TARGET = preset_file_operations_test
CONFIG += console sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += JDSP_TEST_HOOKS

QT += core

INCLUDEPATH += $$PWD/..

SOURCES += \
    preset_file_operations_test.cpp \
    ../data/SafeFileOperations.cpp

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
