#include "AeqPackageValidation.h"

#include "data/SafeFileOperations.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace
{
bool readJsonArray(const QString& path, QJsonArray& array)
{
    const QFileInfo fileInfo(path);
    if(!fileInfo.exists() || fileInfo.isSymLink() || !fileInfo.isFile())
        return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if(error.error != QJsonParseError::NoError || !document.isArray() || document.array().isEmpty())
        return false;
    array = document.array();
    return true;
}

bool validPathComponent(const QJsonValue& value)
{
    return value.isString() && SafeFileOperations::isSafeName(value.toString());
}

bool containedDirectory(const QDir& root, const QString& relativePath)
{
    const QString rootCanonical = QFileInfo(root.path()).canonicalFilePath();
    const QFileInfo candidate(root.filePath(relativePath));
    const QString candidateCanonical = candidate.canonicalFilePath();
    return !rootCanonical.isEmpty() && candidate.exists() && candidate.isDir() &&
        !candidate.isSymLink() &&
        (candidateCanonical == rootCanonical ||
         candidateCanonical.startsWith(rootCanonical + QDir::separator()));
}

bool containedRegularFile(const QDir& root, const QString& relativePath)
{
    const QString rootCanonical = QFileInfo(root.path()).canonicalFilePath();
    const QFileInfo candidate(root.filePath(relativePath));
    const QString candidateCanonical = candidate.canonicalFilePath();
    return !rootCanonical.isEmpty() && candidate.exists() && candidate.isFile() &&
        !candidate.isSymLink() &&
        candidateCanonical.startsWith(rootCanonical + QDir::separator());
}
}

namespace AeqPackageValidation
{
bool publishPackage(const QString& staging, const QString& destination,
                    const RenameOperation& renameOperation)
{
    const RenameOperation rename = renameOperation ? renameOperation :
        [](const QString& source, const QString& target) { return QDir().rename(source, target); };
    const QFileInfo destinationInfo(destination);
    const QString backup = destination + ".backup-" + QUuid::createUuid().toString(QUuid::Id128);
    bool movedExisting = false;
    if(destinationInfo.exists())
    {
        if(!rename(destination, backup))
            return false;
        movedExisting = true;
    }

    if(rename(staging, destination))
    {
        if(movedExisting)
            QDir(backup).removeRecursively();
        return true;
    }

    if(movedExisting)
        rename(backup, destination);
    return false;
}

bool validPackage(const QString& path, const CancellationCheck& cancellationCheck)
{
    const QDir directory(path);
    if(!directory.exists())
        return false;

    QJsonArray version;
    QJsonArray index;
    if(!readJsonArray(directory.filePath("version.json"), version) ||
       !readJsonArray(directory.filePath("index.json"), index))
        return false;
    const QJsonObject versionObject = version.first().toObject();
    if(versionObject.isEmpty() || !versionObject.value("package_url").isString() ||
       versionObject.value("package_url").toString().isEmpty())
        return false;

    for(const QJsonValue& value : index)
    {
        if(cancellationCheck && cancellationCheck())
            return false;
        if(!value.isObject())
            return false;
        const QJsonObject item = value.toObject();
        if(!validPathComponent(item.value("n")) || !validPathComponent(item.value("s")) ||
           !item.value("r").isDouble())
            return false;
        const QString measurementPath = item.value("n").toString() + QDir::separator() +
            item.value("s").toString();
        const QDir measurementDirectory(directory.filePath(measurementPath));
        if(!containedDirectory(directory, measurementPath) ||
           (!containedRegularFile(directory, measurementPath + QDir::separator() + "raw.csv") &&
            !containedRegularFile(directory, measurementPath + QDir::separator() + "graphic.txt")))
            return false;
    }
    return true;
}
}
