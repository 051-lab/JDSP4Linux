TEMPLATE = app
TARGET = dsp_host_reload_test
CONFIG += console HEADLESS
CONFIG -= app_bundle
DEFINES += HEADLESS

QT += core
CONFIG += link_pkgconfig
PKGCONFIG += glib-2.0 gobject-2.0

INCLUDEPATH += $$PWD/.. \
    $$PWD/../audio/base \
    $$PWD/../../libjamesdsp \
    $$PWD/../../libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp

isEmpty(DSP_LIB_DIR) {
    error("dsp_host_reload_test requires DSP_LIB_DIR pointing to a built libjamesdsp directory")
}
LIBS += -L$$DSP_LIB_DIR -llibjamesdsp -lm -ldl -lpthread
PRE_TARGETDEPS += $$DSP_LIB_DIR/liblibjamesdsp.a

SOURCES += \
    dsp_host_reload_test.cpp \
    ../audio/base/DspHost.cpp \
    ../audio/base/BenchmarkWorker.cpp \
    ../audio/base/Utils.cpp \
    ../config/AppConfig.cpp \
    ../config/ConfigContainer.cpp \
    ../config/ConfigIO.cpp \
    ../utils/Log.cpp

RESOURCES += ../../resources/resources.qrc

HEADERS += \
    ../audio/base/DspHost.h \
    ../audio/base/BenchmarkWorker.h \
    ../config/AppConfig.h \
    ../config/DspConfig.h

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
