#include <cassert>
#include <cmath>
#include <limits>

#include <QCoreApplication>

#include "data/PresetProvider.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    assert(PresetProvider::EQ::reverseLookup(QVector<double>{}) .isEmpty());
    assert(PresetProvider::EQ::reverseLookup(QVector<double>(14, 0.0)).isEmpty());
    QVector<double> nonFinite(15, 0.0);
    nonFinite[3] = std::numeric_limits<double>::quiet_NaN();
    assert(PresetProvider::EQ::reverseLookup(nonFinite).isEmpty());
    assert(PresetProvider::EQ::reverseLookup(PresetProvider::EQ::defaultPreset()) ==
           PresetProvider::EQ::defaultPresetName());
    return 0;
}
