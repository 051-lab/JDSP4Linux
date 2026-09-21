QT += core widgets
CONFIG += console c++17 sanitize_address sanitize_undefined
TEMPLATE = app
TARGET = codecontainer_save_test
INCLUDEPATH += .. ../subprojects/EELEditor/src
SOURCES += codecontainer_save_test.cpp
DEFINES += HEADLESS EELEDITOR_TEST_HOOKS
