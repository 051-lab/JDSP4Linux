QT += core widgets
CONFIG += console c++17 sanitize_address sanitize_undefined
TEMPLATE = app
TARGET = eel_parser_test
INCLUDEPATH += .. ../audio/base ../subprojects/EELEditor/src
SOURCES += eel_parser_test.cpp ../data/EelParser.cpp ../utils/Log.cpp
HEADERS += ../data/EelParser.h

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
