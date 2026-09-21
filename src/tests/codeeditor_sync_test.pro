TEMPLATE = app
TARGET = codeeditor_sync_test
CONFIG += console
CONFIG -= app_bundle

QT += core gui widgets

INCLUDEPATH += $$PWD/../subprojects/EELEditor/src
INCLUDEPATH += $$PWD/../subprojects/EELEditor/3rdparty/QCodeEditor/include

SOURCES += \
    codeeditor_sync_test.cpp \
    ../subprojects/EELEditor/src/widgets/codeeditor.cpp \
    ../subprojects/EELEditor/src/widgets/projectview.cpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/src/QCodeEditor.cpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/src/QFramedTextAttribute.cpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/src/QLanguage.cpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/src/QLineNumberArea.cpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/src/QStyleSyntaxHighlighter.cpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/src/QSyntaxStyle.cpp \
    ../subprojects/EELEditor/src/utils/stringutils.cpp

HEADERS += \
    ../subprojects/EELEditor/src/widgets/codeeditor.h \
    ../subprojects/EELEditor/src/widgets/projectview.h \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/include/QCodeEditor.hpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/include/QFramedTextAttribute.hpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/include/QLanguage.hpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/include/QLineNumberArea.hpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/include/QStyleSyntaxHighlighter.hpp \
    ../subprojects/EELEditor/3rdparty/QCodeEditor/include/QSyntaxStyle.hpp

RESOURCES += ../subprojects/EELEditor/3rdparty/QCodeEditor/resources/qcodeeditor_resources.qrc

QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
QMAKE_LFLAGS += -fsanitize=address,undefined
