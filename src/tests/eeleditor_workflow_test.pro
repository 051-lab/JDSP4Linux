TEMPLATE = app
TARGET = eeleditor_workflow_test
CONFIG += console c++17
CONFIG -= app_bundle
QT += core gui widgets testlib

INCLUDEPATH += $$PWD/../subprojects/EELEditor/src \
    $$PWD/../subprojects/EELEditor/3rdparty/QCodeEditor/include \
    $$PWD/../subprojects/EELEditor/3rdparty/docking-system/src

SOURCES += eeleditor_workflow_test.cpp

isEmpty(EELEDITOR_LIB_DIR) {
    EELEDITOR_LIB_DIR = /home/soloarch/Workspace/build/jamesdsp-luna-t15-eeleditor-sync
}
LIBS += -L$$EELEDITOR_LIB_DIR -leeleditor
LIBS += -lxcb
QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
DEFINES += EELEDITOR_TEST_HOOKS
