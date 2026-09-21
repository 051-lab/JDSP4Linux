#include <cassert>

#include <QApplication>
#include <QFile>
#include <QListWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "FileSelectionWidget.h"
#include "data/SafeFileOperations.h"

static QByteArray readFile(const QString& path)
{
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());
    const QString sourceDirectory = temporary.path() + "/source";
    const QString bookmarkDirectory = temporary.path() + "/bookmarks";
    assert(QDir().mkpath(sourceDirectory));
    assert(QDir().mkpath(bookmarkDirectory));
    const QString source = sourceDirectory + "/preset.conf";
    const QByteArray contents("preset bytes\n");
    QFile sourceFile(source);
    assert(sourceFile.open(QIODevice::WriteOnly));
    assert(sourceFile.write(contents) == contents.size());
    sourceFile.close();

    FileSelectionWidget widget;
    widget.setFileTypes({"*.conf"});
    widget.setBookmarkDirectory(QDir(bookmarkDirectory));
    widget.setCurrentFile(source);
    QSignalSpy bookmarked(&widget, &FileSelectionWidget::bookmarkAdded);
    auto* bookmarkButton = widget.findChild<QPushButton*>("bookmark");
    auto* removeButton = widget.findChild<QPushButton*>("remove");
    auto* fileView = widget.findChild<QListWidget*>("fileview");
    assert(bookmarkButton && removeButton && fileView);

    /* A file already in the favorites directory is a successful identity
     * no-op, not a remove-then-copy operation. */
    const QString favoriteSource = bookmarkDirectory + "/favorite.conf";
    const QByteArray favoriteBytes("favorite bytes\n");
    QFile favoriteFile(favoriteSource);
    assert(favoriteFile.open(QIODevice::WriteOnly));
    assert(favoriteFile.write(favoriteBytes) == favoriteBytes.size());
    favoriteFile.close();
    widget.setCurrentFile(favoriteSource);
    bookmarkButton->click();
    assert(bookmarked.count() == 1);
    assert(bookmarked.at(0).at(0).toString() == favoriteSource);
    assert(readFile(favoriteSource) == favoriteBytes);

    bookmarked.clear();
    widget.setCurrentFile(source);
    widget.clearCurrentFile();
    QSignalSpy changed(&widget, &FileSelectionWidget::fileChanged);
    fileView->setCurrentRow(0);
    assert(changed.count() == 1);
    assert(changed.at(0).at(0).toString() == source);
    assert(widget.currentFile().has_value() && widget.currentFile().value() == source);

    bookmarkButton->click();
    const QString bookmarkedPath = bookmarkDirectory + "/preset.conf";
    assert(bookmarked.count() == 1);
    assert(bookmarked.at(0).at(0).toString() == bookmarkedPath);
    assert(readFile(bookmarkedPath) == contents);

    QFile::remove(bookmarkedPath);
    SafeFileOperations::setCopyFailureForTests(true);
    bookmarkButton->click();
    assert(bookmarked.count() == 1);
    assert(!QFile::exists(bookmarkedPath));

    SafeFileOperations::setRemoveFailureForTests(true);
    removeButton->click();
    assert(QFile::exists(source));

    removeButton->click();
    assert(!QFile::exists(source));

    const QString renameSource = sourceDirectory + "/rename-source.conf";
    const QString occupiedPath = sourceDirectory + "/occupied.conf";
    QFile renameFile(renameSource);
    assert(renameFile.open(QIODevice::WriteOnly));
    assert(renameFile.write("rename bytes\n") > 0);
    renameFile.close();
    QFile occupiedFile(occupiedPath);
    assert(occupiedFile.open(QIODevice::WriteOnly));
    assert(occupiedFile.write("occupied bytes\n") > 0);
    occupiedFile.close();

    auto renameThroughDialog = [&](const QString& newName) {
        QTimer::singleShot(0, [newName] {
            for (QWidget* topLevel : QApplication::topLevelWidgets())
            {
                auto* dialog = qobject_cast<QInputDialog*>(topLevel);
                if (dialog && dialog->isVisible())
                {
                    dialog->findChild<QLineEdit*>()->setText(newName);
                    assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
                    return;
                }
            }
        });
        auto* renameButton = widget.findChild<QPushButton*>("rename");
        assert(renameButton);
        renameButton->click();
    };

    widget.setCurrentFile(renameSource);
    renameThroughDialog("renamed.conf");
    const QString renamedPath = sourceDirectory + "/renamed.conf";
    assert(!QFile::exists(renameSource));
    assert(readFile(renamedPath) == "rename bytes\n");

    renameThroughDialog("occupied.conf");
    assert(readFile(renamedPath) == "rename bytes\n");
    assert(readFile(occupiedPath) == "occupied bytes\n");

    renameThroughDialog("../escape.conf");
    assert(readFile(renamedPath) == "rename bytes\n");
    assert(!QFile::exists(temporary.path() + "/escape.conf"));

    widget.clearCurrentFile();
    bookmarkButton->click();
    removeButton->click();
    return 0;
}
