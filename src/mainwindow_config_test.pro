include(src.pro)

isEmpty(DSP_LIB_DIR) {
    error("mainwindow_config_test requires DSP_LIB_DIR pointing to a current libjamesdsp build")
}

LIBS -= -L$$OUT_PWD/../libjamesdsp
LIBS -= -llibjamesdsp
LIBS += -L$$DSP_LIB_DIR -llibjamesdsp
PRE_TARGETDEPS -= $$OUT_PWD/../libjamesdsp/liblibjamesdsp.a
PRE_TARGETDEPS += $$DSP_LIB_DIR/liblibjamesdsp.a

QT += testlib
TARGET = mainwindow_config_integration_test
CONFIG += console
CONFIG -= app_bundle

SOURCES -= main.cpp
SOURCES += tests/mainwindow_config_integration_test.cpp
