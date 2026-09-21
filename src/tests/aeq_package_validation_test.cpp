#include <cassert>
#include <functional>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "AeqPackageValidation.h"

namespace
{
void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(data) == data.size());
}

void writeJson(const QString& path, const QJsonArray& value)
{
    writeFile(path, QJsonDocument(value).toJson(QJsonDocument::Compact));
}

void createValidPackage(const QString& root, bool graphicOnly = false)
{
    writeJson(QDir(root).filePath("version.json"),
              {QJsonObject{{"package_url", "https://example.invalid/eq.tar.gz"},
                           {"package_time", "2026-09-15"}}});
    writeJson(QDir(root).filePath("index.json"),
              {QJsonObject{{"n", "headphone"}, {"s", "source"}, {"r", 1}}});
    const QString measurement = QDir(root).filePath("headphone/source");
    assert(QDir().mkpath(measurement));
    writeFile(QDir(measurement).filePath(graphicOnly ? "graphic.txt" : "raw.csv"), "data\n");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());
    const QString root = temporary.path();

    createValidPackage(root);
    assert(AeqPackageValidation::validPackage(root));

    int cancellationChecks = 0;
    const bool cancelledValidation = AeqPackageValidation::validPackage(root, [&]() {
        ++cancellationChecks;
        return true;
    });
    assert(!cancelledValidation);
    assert(cancellationChecks == 1);

    QFile::remove(QDir(root).filePath("headphone/source/raw.csv"));
    assert(!AeqPackageValidation::validPackage(root));
    writeFile(QDir(root).filePath("headphone/source/graphic.txt"), "graphic\n");
    assert(AeqPackageValidation::validPackage(root));

    writeJson(QDir(root).filePath("index.json"),
              {QJsonObject{{"n", "../escape"}, {"s", "source"}, {"r", 1}}});
    assert(!AeqPackageValidation::validPackage(root));
    writeJson(QDir(root).filePath("index.json"), {QJsonObject{{"n", "headphone"}, {"r", 1}}});
    assert(!AeqPackageValidation::validPackage(root));
    writeFile(QDir(root).filePath("index.json"), "not json");
    assert(!AeqPackageValidation::validPackage(root));

    createValidPackage(root);
    const QString outside = root + "-outside.csv";
    writeFile(outside, "outside\n");
    const QString rawPath = QDir(root).filePath("headphone/source/raw.csv");
    assert(QFile::remove(QDir(root).filePath("headphone/source/graphic.txt")));
    assert(QFile::remove(rawPath));
    assert(QFile::link(outside, rawPath));
    assert(QFileInfo(rawPath).isSymLink());
    assert(!AeqPackageValidation::validPackage(root));
    assert(QFile::remove(rawPath));
    writeFile(rawPath, "data\n");

    const QString publishRoot = temporary.path() + "/publish";
    const QString destination = publishRoot + "/database";
    const QString staging = publishRoot + "/staging";
    assert(QDir().mkpath(destination));
    writeFile(QDir(destination).filePath("old-state"), "preserve\n");
    assert(!AeqPackageValidation::publishPackage(staging, destination));
    assert(QFile::exists(QDir(destination).filePath("old-state")));
    assert(!QFile::exists(staging));

    assert(QDir().mkpath(staging));
    writeFile(QDir(staging).filePath("blocked-state"), "blocked\n");
    bool injectedBackupFailure = false;
    const AeqPackageValidation::RenameOperation failBackup =
        [&](const QString& source, const QString& target) {
            if(source == destination)
            {
                injectedBackupFailure = true;
                return false;
            }
            return QDir().rename(source, target);
        };
    assert(!AeqPackageValidation::publishPackage(staging, destination, failBackup));
    assert(injectedBackupFailure);
    assert(QFile::exists(QDir(destination).filePath("old-state")));
    assert(QFile::exists(QDir(staging).filePath("blocked-state")));

    assert(QDir().mkpath(staging));
    writeFile(QDir(staging).filePath("new-state"), "publish\n");
    assert(AeqPackageValidation::publishPackage(staging, destination));
    assert(QFile::exists(QDir(destination).filePath("new-state")));
    assert(!QFile::exists(QDir(destination).filePath("old-state")));

    assert(QDir().mkpath(staging));
    writeFile(QDir(staging).filePath("replacement-state"), "replacement\n");
    writeFile(QDir(destination).filePath("preserve-on-failure"), "preserve\n");
    bool injectedPublishFailure = false;
    const AeqPackageValidation::RenameOperation failPublish =
        [&](const QString& source, const QString& target) {
            if(source == staging && target == destination)
            {
                injectedPublishFailure = true;
                return false;
            }
            return QDir().rename(source, target);
        };
    assert(!AeqPackageValidation::publishPackage(staging, destination, failPublish));
    assert(injectedPublishFailure);
    assert(QFile::exists(QDir(destination).filePath("new-state")));
    assert(QFile::exists(QDir(destination).filePath("preserve-on-failure")));
    assert(!QFile::exists(QDir(destination).filePath("replacement-state")));
    QFile stagedReplacement(QDir(staging).filePath("replacement-state"));
    assert(stagedReplacement.open(QIODevice::ReadOnly));
    assert(stagedReplacement.readAll() == "replacement\n");

    return 0;
}
