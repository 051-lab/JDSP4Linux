#include "config/ConfigIO.h"

#include <QTemporaryDir>
#include <QVariantMap>

#include <cassert>

int main()
{
    QTemporaryDir temporary;
    assert(temporary.isValid());

    const QString path = temporary.filePath("config.conf");
    QVariantMap values;
    values.insert("enabled", true);
    values.insert("gain", 1.25f);
    values.insert("script", QString("/tmp/example.eel"));

    assert(ConfigIO::writeFile(path, values));
    const QVariantMap loaded = ConfigIO::readFile(path);
    assert(loaded.value("enabled").toBool());
    assert(loaded.value("gain").toFloat() > 1.24f);
    assert(loaded.value("script").toString() == "/tmp/example.eel");
    return 0;
}
