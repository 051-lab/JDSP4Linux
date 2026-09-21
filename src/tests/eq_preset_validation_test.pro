QT += core
CONFIG += console c++17
TEMPLATE = app
TARGET = eq_preset_validation_test
INCLUDEPATH += ..
SOURCES += eq_preset_validation_test.cpp \
           ../data/PresetProvider.cpp
