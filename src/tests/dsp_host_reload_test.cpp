#include <cassert>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "audio/base/DspHost.h"
#include "config/DspConfig.h"

#include <cmath>

extern "C" {
#include <jdsp_header.h>
void JamesDSPProcess(JamesDSPLib*, size_t);
}

static void writeScript(const QString &path, const QByteArray &source)
{
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly | QIODevice::Text));
    assert(file.write(source) == source.size());
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());
    const QString validPath = temporary.path() + "/valid.eel";
    writeScript(validPath, "@init\ngain = 2;\n@sample\nspl0 *= gain; spl1 *= gain;\n");
    const QString recoveredPath = temporary.path() + "/recovered.eel";
    writeScript(recoveredPath, "@init\ngain = 3;\n@sample\nspl0 *= gain; spl1 *= gain;\n");
    const QString invalidPath = temporary.path() + "/invalid.eel";
    writeScript(invalidPath, "@init\ngain = 99;\n@sample\nspl0 = ;\n");
    const QString crlfPath = temporary.path() + "/crlf.eel";
    writeScript(crlfPath, "@sample\r\nspl0 *= gain; spl1 *= gain;\r\n@init\r\ngain = 2;\r\n");

    JamesDSPGlobalMemoryAllocation();
    JamesDSPLib dsp = {};
    JamesDSPInit(&dsp, 16, 48000.0f);
    {
        QList<QList<QString>> compilerResults;
        DspHost host(&dsp, [&compilerResults](DspHost::Message message, std::any value) {
            if (message == DspHost::EelCompilerResult)
                compilerResults.append(std::any_cast<QList<QString>>(value));
        });

        DspConfig config(false);

        config.set(DspConfig::tone_filtertype, 0);
        config.set(DspConfig::tone_interpolation, 0);
        config.set(DspConfig::tone_eq, QStringLiteral("1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0"));
        assert(host.update(&config));
        config.set(DspConfig::tone_eq, QStringLiteral("1;2;3"));
        assert(host.update(&config));
        config.set(DspConfig::tone_eq, QStringLiteral("1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;0;0;0;0;0;0;0;0;0;0;0;0;0;0;nan"));
        assert(host.update(&config));
        for (size_t i = 0; i < 16; ++i)
        {
            dsp.tmpBuffer[0][i] = 0.25f;
            dsp.tmpBuffer[1][i] = -0.25f;
        }
        JamesDSPProcess(&dsp, 16);
        for (size_t i = 0; i < 16; ++i)
            assert(std::isfinite(dsp.tmpBuffer[0][i]) && std::isfinite(dsp.tmpBuffer[1][i]));

        config.set(DspConfig::compander_response,
                   QStringLiteral("95;200;400;800;1600;3400;7500;0;0;0;0;0;0;0"));
        assert(host.update(&config));
        config.set(DspConfig::compander_response,
                   QStringLiteral("95;200;400"));
        assert(host.update(&config));
        config.set(DspConfig::compander_response,
                   QStringLiteral("95;200;400;800;1600;3400;7500;0;0;0;0;0;0;nan"));
        assert(host.update(&config));
        for (size_t i = 0; i < 16; ++i)
        {
            dsp.tmpBuffer[0][i] = 0.125f;
            dsp.tmpBuffer[1][i] = -0.125f;
        }
        JamesDSPProcess(&dsp, 16);
        for (size_t i = 0; i < 16; ++i)
            assert(std::isfinite(dsp.tmpBuffer[0][i]) && std::isfinite(dsp.tmpBuffer[1][i]));

        config.set(DspConfig::liveprog_file, temporary.path() + "/missing-initial.eel");
        config.set(DspConfig::liveprog_enable, true);
        host.reloadLiveprog(&config);
        assert(!host.liveprogActive());
        assert(!dsp.liveprogEnabled);
        assert(!compilerResults.isEmpty() && compilerResults.constLast().at(0).toInt() < 0);
        dsp.tmpBuffer[0][0] = 1.0f;
        dsp.tmpBuffer[1][0] = 1.0f;
        LiveProgProcess(&dsp, 1);
        assert(dsp.tmpBuffer[0][0] == 1.0f && dsp.tmpBuffer[1][0] == 1.0f);

        config.set(DspConfig::liveprog_file, validPath);
        host.reloadLiveprog(&config);
        assert(host.liveprogActive());
        assert(dsp.liveprogEnabled);
        assert(!compilerResults.isEmpty() && compilerResults.constLast().at(0).toInt() > 0);
        dsp.tmpBuffer[0][0] = 1.0f;
        dsp.tmpBuffer[1][0] = 1.0f;
        LiveProgProcess(&dsp, 1);
        assert(dsp.tmpBuffer[0][0] == 2.0f && dsp.tmpBuffer[1][0] == 2.0f);

        config.set(DspConfig::liveprog_file, crlfPath);
        host.reloadLiveprog(&config);
        assert(dsp.liveprogEnabled);
        assert(compilerResults.constLast().at(0).toInt() > 0);

        config.set(DspConfig::liveprog_file, temporary.path() + "/missing.eel");
        host.reloadLiveprog(&config);
        assert(host.liveprogActive());
        assert(dsp.liveprogEnabled);
        assert(compilerResults.constLast().at(0).toInt() < 0);
        assert(!compilerResults.constLast().at(1).isEmpty());
        assert(compilerResults.constLast().at(1).contains("exist"));
        dsp.tmpBuffer[0][0] = 1.0f;
        dsp.tmpBuffer[1][0] = 1.0f;
        LiveProgProcess(&dsp, 1);
        assert(dsp.tmpBuffer[0][0] == 2.0f && dsp.tmpBuffer[1][0] == 2.0f);

        config.set(DspConfig::liveprog_file, invalidPath);
        host.reloadLiveprog(&config);
        assert(dsp.liveprogEnabled);
        assert(compilerResults.constLast().at(0).toInt() < 0);
        assert(!compilerResults.constLast().at(1).isEmpty());
        dsp.tmpBuffer[0][0] = 1.0f;
        dsp.tmpBuffer[1][0] = 1.0f;
        LiveProgProcess(&dsp, 1);
        assert(dsp.tmpBuffer[0][0] == 2.0f && dsp.tmpBuffer[1][0] == 2.0f);

        config.set(DspConfig::liveprog_file, recoveredPath);
        host.reloadLiveprog(&config);
        assert(dsp.liveprogEnabled);
        dsp.tmpBuffer[0][0] = 1.0f;
        dsp.tmpBuffer[1][0] = 1.0f;
        LiveProgProcess(&dsp, 1);
        assert(dsp.tmpBuffer[0][0] == 3.0f && dsp.tmpBuffer[1][0] == 3.0f);

        config.set(DspConfig::liveprog_file, temporary.path());
        host.reloadLiveprog(&config);
        assert(dsp.liveprogEnabled);
        assert(compilerResults.constLast().at(0).toInt() < 0);
        assert(!compilerResults.constLast().at(1).isEmpty());

        config.set(DspConfig::liveprog_file, validPath);
        config.set(DspConfig::liveprog_enable, false);
        host.reloadLiveprog(&config);
        assert(!dsp.liveprogEnabled);
    }
    JamesDSPFree(&dsp);
    JamesDSPGlobalMemoryDeallocation();
    return 0;
}
