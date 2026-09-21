#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>

#include "data/PresetManager.h"
#include "data/SafeFileOperations.h"
#include "data/model/PresetListModel.h"
#include "data/model/RouteListModel.h"

Route RouteListModel::makeDefaultRoute()
{
    return Route("*", "<any output route>");
}

static void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(contents) == contents.size());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    assert(temp.isValid());

    writeFile(QDir(temp.path()).filePath("studio.conf"), "studio\n");
    writeFile(QDir(temp.path()).filePath("occupied.conf"), "occupied\n");

    PresetManager manager(nullptr, temp.path());
    assert(manager.exists("studio"));
    assert(manager.presetModel()->getList().contains("studio"));
    assert(manager.rename("studio", "renamed"));
    assert(!manager.exists("studio"));
    assert(manager.exists("renamed"));
    assert(manager.presetModel()->getList().contains("renamed"));

    assert(!manager.rename("renamed", "occupied"));
    assert(manager.exists("renamed"));
    assert(manager.exists("occupied"));
    assert(!manager.rename("../renamed", "escaped"));
    SafeFileOperations::setRenameFailureForTests(true);
    assert(!manager.rename("renamed", "injected"));
    assert(manager.exists("renamed"));
    assert(!manager.exists("injected"));
    SafeFileOperations::setRemoveFailureForTests(true);
    assert(!manager.remove("renamed"));
    assert(manager.exists("renamed"));
    assert(manager.remove("renamed"));
    assert(!manager.exists("renamed"));
    assert(!manager.remove("renamed"));
    assert(!manager.remove("../occupied"));
    assert(manager.exists("occupied"));
    return 0;
}
