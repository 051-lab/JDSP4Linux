TEMPLATE = app
TARGET = gzip_downloader_test
CONFIG += console sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += JDSP_TEST_HOOKS

QT += core network testlib widgets

INCLUDEPATH += $$PWD/../subprojects/AutoEqIntegration \
    $$PWD/../../3rdparty/qtpromise/include \
    $$PWD/../../3rdparty/qtpromise/src

SOURCES += \
    gzip_downloader_test.cpp \
    ../subprojects/AutoEqIntegration/GzipDownloader.cpp \
    ../subprojects/AutoEqIntegration/GzipDownloaderDialog.cpp

FORMS += ../subprojects/AutoEqIntegration/FileDownloaderDialog.ui

HEADERS += ../subprojects/AutoEqIntegration/GzipDownloader.h
HEADERS += ../subprojects/AutoEqIntegration/ExtractionThread.h
HEADERS += ../subprojects/AutoEqIntegration/GzipDownloaderDialog.h

LIBS += -larchive

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
