TEMPLATE = app
TARGET = aeq_package_manager_test
CONFIG += console c++17 sanitize_address sanitize_undefined
CONFIG -= app_bundle
DEFINES += HEADLESS

QT += core network widgets

INCLUDEPATH += $$PWD/.. \
    $$PWD/../audio/base \
    $$PWD/../subprojects \
    $$PWD/../subprojects/AutoEqIntegration \
    $$PWD/../../3rdparty/qtpromise/include \
    $$PWD/../../3rdparty/qtpromise/src

SOURCES += \
    aeq_package_manager_test.cpp \
    ../config/AppConfig.cpp \
    ../config/ConfigContainer.cpp \
    ../config/ConfigIO.cpp \
    ../data/SafeFileOperations.cpp \
    ../subprojects/AutoEqIntegration/AeqPackageManager.cpp \
    ../subprojects/AutoEqIntegration/AeqPackageValidation.cpp \
    ../subprojects/AutoEqIntegration/GzipDownloader.cpp \
    ../subprojects/AutoEqIntegration/GzipDownloaderDialog.cpp \
    ../subprojects/AutoEqIntegration/HttpException.cpp \
    ../utils/Log.cpp

HEADERS += \
    ../config/AppConfig.h \
    ../config/ConfigContainer.h \
    ../subprojects/AutoEqIntegration/AeqPackageManager.h \
    ../subprojects/AutoEqIntegration/AeqPackageValidation.h \
    ../subprojects/AutoEqIntegration/ExtractionThread.h \
    ../subprojects/AutoEqIntegration/GzipDownloader.h \
    ../subprojects/AutoEqIntegration/GzipDownloaderDialog.h
HEADERS += ../subprojects/AutoEqIntegration/HttpException.h

FORMS += ../subprojects/AutoEqIntegration/FileDownloaderDialog.ui
LIBS += -larchive
QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
