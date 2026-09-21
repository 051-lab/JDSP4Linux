#include "PresetManager.h"

#include "config/AppConfig.h"
#include "config/DspConfig.h"
#include "model/PresetListModel.h"
#include "SafeFileOperations.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <utility>

PresetManager::PresetManager(QObject *parent, QString presetDirectoryOverride) :
    QObject(parent), _presetModel(new PresetListModel(this, presetDirectoryOverride)),
    _presetDirectoryOverride(std::move(presetDirectoryOverride))
{
    loadRules();
}

bool PresetManager::exists(const QString &name) const
{
    return SafeFileOperations::isSafeName(name) && QFile::exists(presetDirectory() + name + ".conf");
}

bool PresetManager::loadFromPath(const QString &filename)
{
    const QString &src  = filename;
    const QString  dest = AppConfig::instance().getDspConfPath();

    if (!SafeFileOperations::copyAtomically(src, dest))
    {
        this->_presetModel->rescan();
        return false;
    }
    DspConfig::instance().load();
    Log::debug("Loaded " + filename);
    return true;
}


bool PresetManager::load(const QString &name)
{
	if (!SafeFileOperations::isSafeName(name))
		return false;
    return loadFromPath(presetDirectory() + name + ".conf");
}

bool PresetManager::rename(const QString &name, const QString &newName)
{
    if (!SafeFileOperations::isSafeName(name) || !SafeFileOperations::isSafeName(newName))
        return false;
    const QDir directory(presetDirectory());
    if (!SafeFileOperations::renameWithinDirectory(directory, name + ".conf", newName + ".conf"))
        return false;
    this->_presetModel->rescan();
    return true;
}

bool PresetManager::remove(const QString &name)
{
	if (!SafeFileOperations::isSafeName(name))
		return false;
    if (!SafeFileOperations::removeWithinDirectory(QDir(presetDirectory()), name + ".conf"))
        return false;
    this->_presetModel->rescan();
    return true;
}

void PresetManager::save(const QString &name)
{
	if (!SafeFileOperations::isSafeName(name))
		return;
    saveToPath(presetDirectory() + name + ".conf");
}

void PresetManager::saveToPath(const QString &filename)
{
    emit wantsToWriteConfig();

    const QString  src  = AppConfig::instance().getDspConfPath();
    const QString &dest = filename;
    if (!SafeFileOperations::copyAtomically(src, dest))
        return;
    this->_presetModel->rescan();
    Log::debug("Saved to " + filename);
}

void PresetManager::onOutputDeviceChanged(const QString &deviceName, const QString &deviceId, const QString& outputRouteId)
{
    QString defaultRouteId = QString::fromStdString(RouteListModel::makeDefaultRoute().name);
    auto executeRule = [this, deviceName, outputRouteId, defaultRouteId](const PresetRule& rule){
		if (!SafeFileOperations::isSafeName(rule.preset))
			return;
        loadFromPath(AppConfig::instance().getPath("presets/" + rule.preset + ".conf"));
        emit presetAutoloaded(deviceName, rule.routeName, rule.routeId == defaultRouteId);
    };

    // Look for rule with route
    for(const auto& rule : std::as_const(_rules))
    {
        if(rule.deviceId == deviceId && rule.routeId == outputRouteId)
        {
            executeRule(rule);
            return;
        }
    }

    // Fall back to rule with wildcard route
    for(const auto& rule : std::as_const(_rules))
    {
        if(rule.deviceId == deviceId && rule.routeId == defaultRouteId)
        {
            executeRule(rule);
            return;
        }
    }
}

QString PresetManager::rulesPath() const
{
    return AppConfig::instance().getPath("preset_rules.json");
}

QString PresetManager::presetDirectory() const
{
    return _presetDirectoryOverride.isEmpty() ? AppConfig::instance().getPath("presets/") :
                                                 QDir::cleanPath(_presetDirectoryOverride) + QDir::separator();
}

void PresetManager::loadRules()
{
    _rules.clear();

    QFile indexJson(rulesPath());
    if(!indexJson.exists())
    {
        return;
    }

    indexJson.open(QFile::ReadOnly);
    QJsonDocument d = QJsonDocument::fromJson(indexJson.readAll());
    QJsonArray root = d.array();

    for(const auto& item : root)
    {
        _rules.append(PresetRule(item.toObject()));
    }

    indexJson.close();
}

void PresetManager::saveRules() const
{
    QFile json(rulesPath());
    if(!json.open(QIODevice::WriteOnly)){
        Log::error("PresetRuleTableModel::save: Cannot open json file");
        return;
    }

    QJsonArray root;
    for(const auto& item : _rules)
    {
        root.append(item.toJson());
    }

    json.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    json.close();
}

PresetListModel *PresetManager::presetModel() const
{
    return _presetModel;
}

void PresetManager::setRules(const QVector<PresetRule> &newRules)
{
    _rules = newRules;
    saveRules();
}

bool PresetManager::addRule(const PresetRule &rule)
{
    // Only one rule per device & route id
    removeRule(rule.deviceId, rule.routeId);

    _rules.append(rule);
    saveRules();
    return true;
}

void PresetManager::removeRule(const QString &deviceId, const QString &routeId)
{
    for(int i = 0; i < _rules.count(); i++) {
        if(_rules[i].deviceId == deviceId && _rules[i].routeId == routeId) {
            _rules.removeAt(i);
            break;
        }
    }
    saveRules();
}

QVector<PresetRule> PresetManager::rules() const
{
    return _rules;
}
