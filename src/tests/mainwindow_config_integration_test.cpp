#include <cassert>
#include <cmath>
#include <memory>
#include <QApplication>
#include <QCheckBox>
#include <QDBusVariant>
#include <QFile>
#include <QDir>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QtTest/QSignalSpy>

#include "MainWindow.h"
#include "audio/base/IAudioService.h"
#include "audio/base/IAppManager.h"
#include "audio/base/DspHost.h"
#include "config/AppConfig.h"
#include "config/DspConfig.h"
#include "interface/LiveprogSelectionWidget.h"
#include "interface/QAnimatedSlider.h"
#include "subprojects/GraphicEQWidget/GraphicEQWidget/GraphicEQFilterGUI.h"
#include "subprojects/LiquidEqualizerWidget/src/BaseLiquidEqualizerWidget.h"
#include "utils/dbus/IpcHandler.h"

extern "C" {
#include <jdsp_header.h>
}

class TestAppManager final : public IAppManager
{
public:
    QList<AppNode> activeApps() const override { return {}; }
};

class TestAudioService final : public IAudioService
{
public:
    TestAudioService()
    {
        JamesDSPGlobalMemoryAllocation();
        JamesDSPInit(&dsp, 128, 48000.0f);
        engine = std::make_unique<DspHost>(&dsp, [this](DspHost::Message message, std::any value) {
            handleMessage(message, std::move(value));
        });
    }

    ~TestAudioService() override
    {
        engine.reset();
        JamesDSPFree(&dsp);
        JamesDSPGlobalMemoryDeallocation();
    }

    void update(DspConfig *config) override
    {
        engine->update(config);
    }
    void reloadService() override { engine->update(&DspConfig::instance()); }
    IAppManager *appManager() override { return &apps; }
    DspHost *host() override { return engine.get(); }
    std::vector<IOutputDevice> sinkDevices() override { return {}; }
    std::vector<IOutputDevice> outputDevices() override { return {}; }
    DspStatus status() override { return {}; }
    void reloadLiveprogCandidate() { engine->reloadLiveprog(&DspConfig::instance()); }
    float processMonoSample(float sample)
    {
        dsp.tmpBuffer[0][0] = sample;
        dsp.tmpBuffer[1][0] = sample;
        LiveProgProcess(&dsp, 1);
        return dsp.tmpBuffer[0][0];
    }

private:
    TestAppManager apps;
    JamesDSPLib dsp{};
    std::unique_ptr<DspHost> engine;
};

static QStringList fixedFrequencies()
{
    return {"25.0", "40.0", "63.0", "100.0", "160.0", "250.0", "400.0",
            "630.0", "1000.0", "1600.0", "2500.0", "4000.0", "6300.0",
            "10000.0", "16000.0"};
}

static QString fixedEq(const QStringList &gains)
{
    return fixedFrequencies().join(';') + ';' + gains.join(';');
}

static float highPassImpulseFirstSample(float frequency, float sampleRate, float qFactor)
{
    const float x = frequency * 2.0f * float(M_PI) / sampleRate;
    const float sinX = std::sin(x);
    const float cosX = std::cos(x);
    const float a0 = sinX / (qFactor * 2.0f) + 1.0f;
    const float b0 = (1.0f + cosX) / 2.0f;
    return b0 / a0;
}

int main(int argc, char **argv)
{
    QTemporaryDir configRoot(QStringLiteral(
        "/home/soloarch/Workspace/build/jamesdsp-luna-mainwindow-config-XXXXXX"));
    assert(configRoot.isValid());
    assert(QDir().mkpath(configRoot.filePath("jamesdsp")));
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    qputenv("XDG_CACHE_HOME", configRoot.path().toLocal8Bit());

    const QString legacyScriptPath = configRoot.filePath("gainControl.eel");
    QFile legacyScript(legacyScriptPath);
    assert(legacyScript.open(QIODevice::WriteOnly | QIODevice::Text));
    assert(legacyScript.write(
        "desc: Gain control\n"
        "dB:-8<-30,15,1>Volume gain (dB)\n"
        "@init\n"
        "dB = -8; DB_2_LOG = 0.11512925464970228420089957273422; gainLin = exp(dB * DB_2_LOG);\n"
        "@sample\n"
        "spl0 = spl0 * gainLin; spl1 = spl1 * gainLin;\n") > 0);
    legacyScript.close();

    QApplication application(argc, argv);
    AppConfig::instance().set(AppConfig::SetupDone, true);
    AppConfig::instance().set(AppConfig::TrayIconEnabled, false);
    AppConfig::instance().set(AppConfig::BenchmarkOnBoot, false);

    auto &config = DspConfig::instance();
    config.loadDefault();
    config.set(DspConfig::tone_filtertype, 0);
    config.set(DspConfig::tone_eq, fixedEq({"1", "2", "3", "4", "5", "6", "7",
                                            "8", "9", "10", "11", "12", "13", "14", "15"}));
    config.set(DspConfig::liveprog_file, legacyScriptPath);
    config.set(DspConfig::liveprog_enable, true);
    config.save();

    auto *service = new TestAudioService();
    QObject::connect(&config, &DspConfig::updated, service, &IAudioService::update);
    MainWindow window(service, false);
    IpcHandler ipc(service);

    auto *liveprog = window.findChild<LiveprogSelectionWidget *>("liveprog");
    assert(liveprog != nullptr);
    assert(service->host()->liveprogActive());
    assert(liveprog->isActive());

    auto *gainSlider = liveprog->findChild<QAnimatedSlider *>("dB");
    assert(gainSlider != nullptr);
    QSignalSpy compileResults(service, &IAudioService::eelCompilationFinished);
    gainSlider->setValueA(0, false);
    assert(QMetaObject::invokeMethod(gainSlider, "sliderReleased", Qt::DirectConnection));
    assert(gainSlider->valueA() == 0);
    assert(!compileResults.isEmpty() && compileResults.constLast().at(0).toInt() > 0);
    assert(service->host()->liveprogActive() && liveprog->isActive());
    assert(std::abs(service->processMonoSample(1.0f) - 1.0f) < 0.0001f);
    QFile updatedLegacyScript(legacyScriptPath);
    assert(updatedLegacyScript.open(QIODevice::ReadOnly | QIODevice::Text));
    assert(updatedLegacyScript.readAll().contains("dB = 0;"));
    updatedLegacyScript.close();

    QFile invalidCandidate(legacyScriptPath);
    assert(invalidCandidate.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    assert(invalidCandidate.write("@init\ngain = 99;\n@sample\nspl0 = ;\n") > 0);
    invalidCandidate.close();
    service->reloadLiveprogCandidate();
    assert(compileResults.constLast().at(0).toInt() < 0);
    assert(service->host()->liveprogActive() && liveprog->isActive());
    assert(std::abs(service->processMonoSample(1.0f) - 1.0f) < 0.0001f);

    const QString temporarilyMissingPath = legacyScriptPath + ".held";
    assert(QFile::rename(legacyScriptPath, temporarilyMissingPath));
    service->reloadLiveprogCandidate();
    assert(compileResults.constLast().at(0).toInt() < 0);
    assert(compileResults.constLast().at(2).toString().contains("exist"));
    assert(service->host()->liveprogActive() && liveprog->isActive());
    assert(std::abs(service->processMonoSample(1.0f) - 1.0f) < 0.0001f);
    assert(QFile::rename(temporarilyMissingPath, legacyScriptPath));

    QFile restoreLegacyScript(legacyScriptPath);
    assert(restoreLegacyScript.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    assert(restoreLegacyScript.write(
        "desc: Gain control\n"
        "dB:0<-30,15,1>Volume gain (dB)\n"
        "@init\n"
        "dB = 0; DB_2_LOG = 0.11512925464970228420089957273422; gainLin = exp(dB * DB_2_LOG);\n"
        "@sample\n"
        "spl0 = spl0 * gainLin; spl1 = spl1 * gainLin;\n") > 0);
    restoreLegacyScript.close();
    service->reloadLiveprogCandidate();
    assert(compileResults.constLast().at(0).toInt() > 0);
    auto *enableCheckBox = liveprog->findChild<QCheckBox *>("enable");
    assert(enableCheckBox && enableCheckBox->isChecked());
    enableCheckBox->click();
    assert(!liveprog->isActive() && !service->host()->liveprogActive());

    QFile highPassAsset(QStringLiteral("resources/assets/liveprog/highpass200Hz.eel"));
    assert(highPassAsset.open(QIODevice::ReadOnly));
    const QString highPassPath = configRoot.filePath("highpass-control.eel");
    QFile highPassCopy(highPassPath);
    assert(highPassCopy.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray highPassSource = highPassAsset.readAll();
    assert(highPassCopy.write(highPassSource) == highPassSource.size());
    highPassCopy.close();
    config.set(DspConfig::liveprog_file, highPassPath);
    config.set(DspConfig::liveprog_enable, true);
    liveprog->setCurrentLiveprog(highPassPath);
    config.commit();
    assert(service->host()->liveprogActive() && liveprog->isActive());
    auto *frequencySlider = liveprog->findChild<QAnimatedSlider *>("freq");
    assert(frequencySlider && frequencySlider->valueA() == 100);
    const float outputAt100Hz = service->processMonoSample(1.0f);
    assert(std::abs(outputAt100Hz - highPassImpulseFirstSample(100.0f, 48000.0f, 1.0f)) < 0.0001f);
    frequencySlider->setValueA(200, false);
    assert(QMetaObject::invokeMethod(frequencySlider, "sliderReleased", Qt::DirectConnection));
    assert(frequencySlider->valueA() == 200);
    assert(compileResults.constLast().at(0).toInt() > 0);
    assert(service->host()->liveprogActive() && liveprog->isActive());
    const float outputAt200Hz = service->processMonoSample(1.0f);
    assert(std::abs(outputAt200Hz - highPassImpulseFirstSample(200.0f, 48000.0f, 1.0f)) < 0.0001f);
    assert(std::abs(outputAt200Hz - outputAt100Hz) > 0.001f);
    QFile updatedHighPass(highPassPath);
    assert(updatedHighPass.open(QIODevice::ReadOnly | QIODevice::Text));
    assert(updatedHighPass.readAll().contains("freq = 200;"));
    updatedHighPass.close();
    enableCheckBox->click();
    assert(!liveprog->isActive() && !service->host()->liveprogActive());

    const QString precisePath = configRoot.filePath("precise-control.eel");
    QFile preciseScript(precisePath);
    assert(preciseScript.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray preciseSource(
        "gain:0.100<0,1,0.001>Precise gain\n"
        "@init\n"
        "gain = 0.100;\n"
        "@sample\n"
        "spl0 *= gain; spl1 *= gain;\n");
    assert(preciseScript.write(preciseSource) == preciseSource.size());
    preciseScript.close();
    config.set(DspConfig::liveprog_file, precisePath);
    config.set(DspConfig::liveprog_enable, true);
    liveprog->setCurrentLiveprog(precisePath);
    config.commit();
    assert(service->host()->liveprogActive() && liveprog->isActive());
    auto *preciseSlider = liveprog->findChild<QAnimatedSlider *>("gain");
    assert(preciseSlider && preciseSlider->valueA() == 100);
    preciseSlider->setValueA(357, false);
    assert(QMetaObject::invokeMethod(preciseSlider, "sliderReleased", Qt::DirectConnection));
    assert(preciseSlider->valueA() == 357);
    assert(compileResults.constLast().at(0).toInt() > 0);
    QFile updatedPreciseScript(precisePath);
    assert(updatedPreciseScript.open(QIODevice::ReadOnly | QIODevice::Text));
    assert(updatedPreciseScript.readAll().contains("gain = 0.357;"));
    updatedPreciseScript.close();
    assert(std::abs(service->processMonoSample(1.0f) - 0.357f) < 0.0001f);
    enableCheckBox->click();
    assert(!liveprog->isActive() && !service->host()->liveprogActive());

    auto *equalizer = window.findChild<BaseLiquidEqualizerWidget *>("eq_widget");
    assert(equalizer != nullptr);

    const QString valid = fixedEq({"1", "2", "3", "4", "5", "6", "7", "8",
                                   "9", "10", "11", "12", "13", "14", "15"});
    ipc.setAndCommit(QStringLiteral("tone_eq"), QDBusVariant(QVariant(valid)));
    const QVector<double> previousValidBands = equalizer->getBands();
    assert(previousValidBands.size() == 15);
    for (int i = 0; i < previousValidBands.size(); ++i)
        assert(previousValidBands.at(i) == i + 1);

    ipc.setAndCommit(QStringLiteral("tone_eq"),
                     QDBusVariant(QVariant(fixedEq({"-7", "-8", "-9"}))));
    assert(equalizer->getBands() == QVector<double>(15, 0.0));

    QStringList nonFiniteGains = {"1", "2", "3", "4", "5", "6", "7", "8",
                                  "9", "10", "11", "12", "13", "14", "nan"};
    ipc.setAndCommit(QStringLiteral("tone_eq"),
                     QDBusVariant(QVariant(fixedEq(nonFiniteGains))));
    assert(equalizer->getBands() == QVector<double>(15, 0.0));

    const QString malformedFlexible =
        QStringLiteral("30;50;80;125;200;315;500;800;1250;2000;3150;5000;8000;12000;18000;-7;-8;-9");
    ipc.setAndCommit(QStringLiteral("tone_eq"), QDBusVariant(QVariant(malformedFlexible)));
    auto *fixedMode = window.findChild<QRadioButton *>("eq_r_fixed");
    auto *flexMode = window.findChild<QRadioButton *>("eq_r_flex");
    auto *flexEqualizer = window.findChild<GraphicEQFilterGUI *>("eq_dyn_widget");
    assert(fixedMode && flexMode && flexEqualizer);
    assert(!fixedMode->isChecked() && flexMode->isChecked());
    QString neutralCsv;
    flexEqualizer->storeCsv(neutralCsv);
    const QStringList neutralValues = neutralCsv.split(';');
    assert(neutralValues.size() == 30);
    for (int i = 15; i < neutralValues.size(); ++i)
        assert(neutralValues.at(i).toDouble() == 0.0);

    return 0;
}
