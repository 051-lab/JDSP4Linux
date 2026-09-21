TEMPLATE = app
TARGET = file_selection_widget_test
CONFIG += console c++17 sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += JDSP_TEST_HOOKS

QT += core widgets testlib

INCLUDEPATH += $$PWD/.. \
    $$PWD/../interface

SOURCES += \
    file_selection_widget_test.cpp \
    ../interface/FileSelectionWidget.cpp \
    ../data/SafeFileOperations.cpp

HEADERS += \
    ../interface/FileSelectionWidget.h

FORMS += ../interface/FileSelectionWidget.ui

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
