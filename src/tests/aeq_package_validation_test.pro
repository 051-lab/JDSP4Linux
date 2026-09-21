QT += core
CONFIG += console c++17 sanitize_address sanitize_undefined
TEMPLATE = app
TARGET = aeq_package_validation_test
INCLUDEPATH += .. ../subprojects/AutoEqIntegration
SOURCES += aeq_package_validation_test.cpp \
           ../data/SafeFileOperations.cpp \
           ../subprojects/AutoEqIntegration/AeqPackageValidation.cpp
HEADERS += ../subprojects/AutoEqIntegration/AeqPackageValidation.h
QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
