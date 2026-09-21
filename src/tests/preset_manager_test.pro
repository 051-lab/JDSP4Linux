TEMPLATE = app
TARGET = preset_manager_test
CONFIG += console sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += HEADLESS JDSP_TEST_HOOKS

QT += core dbus

INCLUDEPATH += $$PWD/.. $$PWD/../audio/base

SOURCES += \
    preset_manager_test.cpp \
    ../data/PresetManager.cpp \
    ../data/model/PresetListModel.cpp \
    ../data/SafeFileOperations.cpp \
    ../config/AppConfig.cpp \
    ../config/ConfigContainer.cpp \
    ../config/ConfigIO.cpp \
    ../utils/Log.cpp

HEADERS += \
    ../data/PresetManager.h \
    ../data/model/PresetListModel.h \
    ../config/AppConfig.h \
    ../config/DspConfig.h

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
