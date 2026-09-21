TEMPLATE = app
TARGET = liveprog_editor_bridge_test
CONFIG += console c++17 sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += HEADLESS EELEDITOR_TEST_HOOKS

QT += core gui widgets testlib

INCLUDEPATH += $$PWD/.. \
    $$PWD/../interface \
    $$PWD/../audio/base \
    $$PWD/../subprojects/EELEditor/src \
    $$PWD/../subprojects/EELEditor/3rdparty/QCodeEditor/include \
    $$PWD/../subprojects/EELEditor/3rdparty/docking-system/src

SOURCES += \
    liveprog_editor_bridge_test.cpp \
    liveprog_editor_bridge_stubs.cpp \
    ../interface/LiveprogSelectionWidget.cpp \
    ../interface/QAnimatedSlider.cpp \
    ../interface/FileSelectionWidget.cpp \
    ../interface/WidgetMarqueeLabel.cpp \
    ../data/EelParser.cpp \
    ../data/SafeFileOperations.cpp \
    ../config/AppConfig.cpp \
    ../config/ConfigContainer.cpp \
    ../config/ConfigIO.cpp \
    ../utils/Log.cpp

HEADERS += \
    ../interface/LiveprogSelectionWidget.h \
    ../interface/QAnimatedSlider.h \
    ../interface/FileSelectionWidget.h \
    ../interface/WidgetMarqueeLabel.h \
    ../data/EelParser.h \
    ../data/SafeFileOperations.h \
    ../config/AppConfig.h \
    ../config/ConfigContainer.h \
    ../config/ConfigIO.h \
    ../interface/event/ScrollFilter.h \
    ../subprojects/EELEditor/src/eeleditor.h

FORMS += \
    ../interface/LiveprogSelectionWidget.ui \
    ../interface/FileSelectionWidget.ui

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
