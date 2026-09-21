#include "config/DspConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cassert>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    DspConfig config(false);

    bool exists = true;
    assert(config.get<int>(DspConfig::bass_maxgain, &exists) == 5);
    assert(!exists);
    assert(config.get<int>(DspConfig::bass_maxgain) == 5);

    config.set(DspConfig::bass_maxgain, 9);
    assert(config.get<int>(DspConfig::bass_maxgain, &exists) == 9);
    assert(exists);

    assert(config.get<int>(DspConfig::bass_maxgain, &exists, false) == 9);
    assert(exists);
    assert(config.get<int>(DspConfig::tone_enable, &exists, false) == 0);
    assert(!exists);

    QTemporaryDir configHome;
    assert(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    assert(QDir().mkpath(configHome.path() + "/jamesdsp"));
    int buffered = 0;
    int updated = 0;
    QObject::connect(&config, &DspConfig::configBuffered, [&]() { ++buffered; });
    QObject::connect(&config, &DspConfig::updated, [&](DspConfig*) { ++updated; });
    config.load(QStringLiteral("master_enable=false\ncompander_response=\"1;2;3\"\n"));
    assert(buffered == 1);
    assert(updated == 1);
    assert(!config.get<bool>(DspConfig::master_enable));
    assert(config.get<QString>(DspConfig::compander_response) == QStringLiteral("\"1;2;3\""));
    assert(QFileInfo::exists(configHome.path() + "/jamesdsp/audio.conf"));

    config.load(QStringLiteral("[malformed\nnot-a-key\n"));
    assert(buffered == 2);
    assert(updated == 2);
    assert(config.get<int>(DspConfig::bass_maxgain) == 5);
    return 0;
}
