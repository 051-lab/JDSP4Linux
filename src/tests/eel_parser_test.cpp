#include <cassert>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "data/EelParser.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    assert(temp.isValid());
    const QString path = temp.path() + "/metadata.eel";
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    file.write("pregain = 7;\n// gain = 9;\n/*\ngain = 8;\n*/\nmissing:0<0,1,0.1>Missing\nlate:0.5<0,1,0.25>Late\noffgrid:0.123<0,1,0.01>Off-grid default\nexpression:0<0,1,0.1>Expression assignment\ninvalid:0<0,1,0>Invalid\ninvalidRange:1<2,1,0.1>Invalid range\nnegativeStep:1<0,2,-0.1>Negative step\n\tfine:0.1<0.1,1,0.0001>Fine\n\ttiny:0.1<0.1,1,0.001>Tiny\n\tgain:0<-1,1,0.001>Gain\ninteger:2<0,4,1>Integer\nmode = 1;\nmode:1<0,2,1{A,B,C}>Mode\nmode:2<0,2,1{X,Y,Z}>Duplicate mode\n@init\ngain = 0;\nlate = 0.5;\noffgrid = 0.123;\nexpression = other;\ninvalid = 0;\ninvalidRange = 1;\nnegativeStep = 1;\nfine = 0.10005;\ntiny = 0.1;\npregain = 7;\ninteger = 2;\nmode = 1;\n@sample\nspl0 *= gain; spl1 *= gain;\n");
    file.close();

    EELParser parser;
    assert(parser.loadFile(path));
    assert(parser.getDiagnostics().join('\n').contains("expression"));
    EELBaseProperty *gain = nullptr;
    for (EELBaseProperty *property : parser.getProperties())
        if (property->getKey() == "gain")
            gain = property;
    assert(gain != nullptr);
    auto *number = dynamic_cast<EELNumberRangeProperty<float> *>(gain);
    assert(number != nullptr);
    assert(std::abs(number->getValue()) < 0.00001f);
    number->setValue(0.1234f);
    assert(parser.manipulateProperty(number));

    EELBaseProperty *fine = nullptr;
    EELBaseProperty *late = nullptr;
    EELBaseProperty *offgrid = nullptr;
    EELBaseProperty *invalid = nullptr;
    EELBaseProperty *mode = nullptr;
    EELBaseProperty *tiny = nullptr;
    for (EELBaseProperty *property : parser.getProperties())
    {
        if (property->getKey() == "fine")
            fine = property;
        if (property->getKey() == "late")
            late = property;
        if (property->getKey() == "offgrid")
            offgrid = property;
        if (property->getKey() == "invalid")
            invalid = property;
        if (property->getKey() == "mode")
            mode = property;
        if (property->getKey() == "tiny")
            tiny = property;
    }
    assert(fine != nullptr);
    assert(late != nullptr);
    assert(offgrid != nullptr);
    auto *offgridNumber = dynamic_cast<EELNumberRangeProperty<float> *>(offgrid);
    assert(offgridNumber != nullptr);
    assert(std::abs(offgridNumber->getDefault() - 0.12f) < 0.00001f);
    assert(invalid == nullptr);
    assert(mode != nullptr);
    assert(tiny != nullptr);
    auto *tinyNumber = dynamic_cast<EELNumberRangeProperty<float> *>(tiny);
    assert(tinyNumber != nullptr);
    assert(std::abs(tinyNumber->getMinimum() - 0.1f) < 0.00001f);
    assert(std::abs(tinyNumber->getStep() - 0.001f) < 0.00001f);
    tinyNumber->setValue(0.1006f);
    assert(std::abs(tinyNumber->getValue() - 0.101f) < 0.00001f);
    assert(parser.manipulateProperty(tinyNumber));
    auto *list = dynamic_cast<EELListProperty *>(mode);
    assert(list != nullptr);
    assert(list->getOptions() == QStringList({"A", "B", "C"}));
    int modeCount = 0;
    for (EELBaseProperty *property : parser.getProperties())
        if (property->getKey() == "mode")
            ++modeCount;
    assert(modeCount == 1);
    auto *fineNumber = dynamic_cast<EELNumberRangeProperty<float> *>(fine);
    assert(fineNumber != nullptr);
    fineNumber->setValue(0.10005f);
    assert(parser.manipulateProperty(fineNumber));
    auto *lateNumber = dynamic_cast<EELNumberRangeProperty<float> *>(late);
    assert(lateNumber != nullptr);
    lateNumber->setValue(0.76f);
    assert(std::abs(lateNumber->getValue() - 0.75f) < 0.00001f);
    assert(parser.manipulateProperty(lateNumber));

    QFile saved(path);
    assert(saved.open(QIODevice::ReadOnly));
    const QByteArray source = saved.readAll();
    assert(source.contains("gain = 0.123;"));
    assert(source.contains("fine = 0.1001;"));
    assert(source.contains("late = 0.75;"));
    assert(source.contains("tiny = 0.101;"));
    assert(source.contains("pregain = 7;"));
    assert(source.contains("// gain = 9;"));
    return 0;
}
