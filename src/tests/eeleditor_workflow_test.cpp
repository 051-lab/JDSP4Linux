#include <cassert>

#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "eeleditor.h"
#include "model/codecontainer.h"
#include "widgets/codeeditor.h"
#include "widgets/projectview.h"

static void writeScript(const QString& path, const QByteArray& source)
{
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly | QIODevice::Text));
    assert(file.write(source) == source.size());
}

static QByteArray readBytes(const QString &path)
{
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

static void editDocument(CodeEditor *editor, const QString &source)
{
    editor->selectAll();
    editor->insertPlainText(source);
    assert(editor->document()->isModified());
}

static void dismissSaveErrorDialog(const QString &expectedPath)
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(1);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, expectedPath] {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            auto *message = qobject_cast<QMessageBox *>(widget);
            if (message && message->isVisible())
            {
                assert(message->text().contains(expectedPath));
                message->accept();
                timer->stop();
                timer->deleteLater();
                return;
            }
        }
    });
    timer->start();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());

    const QString firstPath = temporary.filePath("first.eel");
    const QString secondPath = temporary.filePath("second.eel");
    writeScript(firstPath, "@init\nfirst = 1;\n@sample\nspl0 = spl0;\n");
    writeScript(secondPath, "@init\nsecond = 2;\n@sample\nspl1 = spl1;\n");

    EELEditor editor;
    QSignalSpy executions(&editor, &EELEditor::executionRequested);
    QSignalSpy saves(&editor, &EELEditor::scriptSaved);
    editor.openNewScript(firstPath);

    auto* codeEditor = editor.findChild<CodeEditor*>();
    auto* projectView = editor.findChild<ProjectView*>();
    assert(codeEditor != nullptr);
    assert(projectView != nullptr);
    assert(projectView->count() == 1);

    editDocument(codeEditor, "@init\nfirst = 7;\n@sample\nspl0 = spl0;\n");
    editor.runCode();
    assert(executions.count() == 1);
    assert(executions.at(0).at(0).toString() == firstPath);
    QFile first(firstPath);
    assert(first.open(QIODevice::ReadOnly));
    assert(first.readAll().contains("first = 7;"));
    assert(!codeEditor->document()->isModified());

    const QString saveAsPath = temporary.filePath("saved-as.eel");
    editDocument(codeEditor, "@init\nfirst = 8;\n@sample\nspl0 = spl0;\n");
    QTimer::singleShot(0, [saveAsPath] {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (dialog && dialog->isVisible())
            {
                dialog->selectFile(saveAsPath);
                assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
                return;
            }
        }
    });
    editor.saveProjectAs();
    /* The static file-dialog helper may normalize the temporary path; inspect
     * the editor's adopted container path as the authority. */
    const QString adoptedPath = projectView->getCurrentFile()->path;
    assert(adoptedPath == saveAsPath);
    assert(projectView->currentItem()->toolTip() == saveAsPath);
    QFile originalAfterSaveAs(firstPath);
    assert(originalAfterSaveAs.open(QIODevice::ReadOnly));
    assert(originalAfterSaveAs.readAll().contains("first = 7;"));
    QFile adopted(adoptedPath);
    assert(adopted.open(QIODevice::ReadOnly));
    assert(adopted.readAll().contains("first = 8;"));
    assert(saves.count() == 1);
    assert(!codeEditor->document()->isModified());
    editDocument(codeEditor, "@init\nfirst = 10;\n@sample\nspl0 = spl0;\n");
    editor.saveProject();
    QFile savedAgain(adoptedPath);
    assert(savedAgain.open(QIODevice::ReadOnly));
    assert(savedAgain.readAll().contains("first = 10;"));
    assert(saves.count() == 2);
    assert(!codeEditor->document()->isModified());

    const QString pathBeforeCancelledSaveAs = projectView->getCurrentFile()->path;
    const int savesBeforeCancelledSaveAs = saves.count();
    editDocument(codeEditor, "@init\nfirst = 10.5;\n@sample\nspl0 = spl0;\n");
    QTimer::singleShot(0, [] {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            auto *dialog = qobject_cast<QFileDialog *>(widget);
            if (dialog && dialog->isVisible())
            {
                assert(QMetaObject::invokeMethod(dialog, "reject", Qt::DirectConnection));
                return;
            }
        }
    });
    editor.saveProjectAs();
    assert(projectView->getCurrentFile()->path == pathBeforeCancelledSaveAs);
    assert(projectView->currentItem()->toolTip() == pathBeforeCancelledSaveAs);
    assert(saves.count() == savesBeforeCancelledSaveAs);
    assert(codeEditor->document()->isModified());
    assert(codeEditor->toPlainText().contains("first = 10.5;"));
    /* Restore the existing fixture's expected dirty edit for subsequent save
     * failure assertions, without committing the cancelled candidate. */
    editDocument(codeEditor, "@init\nfirst = 11;\n@sample\nspl0 = spl0;\n");

    const QByteArray lastSavedBytes = readBytes(adoptedPath);
    const QString failedEdit = "@init\nfirst = 11;\n@sample\nspl0 = spl0;\n";
    editDocument(codeEditor, failedEdit);
    CodeContainerSetSaveCommitFailureForTests(true);
    dismissSaveErrorDialog(adoptedPath);
    editor.saveProject();
    CodeContainerSetSaveCommitFailureForTests(false);
    assert(codeEditor->document()->isModified());
    assert(projectView->getCurrentFile()->code == failedEdit);
    assert(readBytes(adoptedPath) == lastSavedBytes);
    assert(saves.count() == 2);

    CodeContainerSetSaveCommitFailureForTests(true);
    dismissSaveErrorDialog(adoptedPath);
    editor.runCode();
    CodeContainerSetSaveCommitFailureForTests(false);
    assert(executions.count() == 1);
    assert(codeEditor->document()->isModified());
    assert(projectView->getCurrentFile()->code == failedEdit);
    assert(readBytes(adoptedPath) == lastSavedBytes);

    const QString failedSaveAsPath = temporary.filePath("failed-save-as.eel");
    QTimer::singleShot(0, [failedSaveAsPath] {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (dialog && dialog->isVisible())
            {
                dialog->selectFile(failedSaveAsPath);
                assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
                return;
            }
        }
    });
    CodeContainerSetSaveCommitFailureForTests(true);
    dismissSaveErrorDialog(failedSaveAsPath);
    editor.saveProjectAs();
    CodeContainerSetSaveCommitFailureForTests(false);
    assert(projectView->getCurrentFile()->path == adoptedPath);
    assert(projectView->currentItem()->toolTip() == adoptedPath);
    assert(!QFile::exists(failedSaveAsPath));
    assert(codeEditor->document()->isModified());
    assert(projectView->getCurrentFile()->code == failedEdit);
    assert(saves.count() == 2);

    editor.openNewScript(secondPath);
    assert(projectView->count() == 2);
    projectView->setCurrentRow(0);
    QCoreApplication::processEvents();
    assert(projectView->getCurrentFile()->path == adoptedPath);
    editDocument(codeEditor, "@init\nfirst = 9;\n@sample\nspl0 = spl0;\n");
    projectView->setCurrentRow(1);
    QCoreApplication::processEvents();
    assert(projectView->getCurrentFile()->path == secondPath);
    projectView->setCurrentRow(0);
    QCoreApplication::processEvents();
    assert(projectView->getCurrentFile()->path == adoptedPath);
    assert(codeEditor->toPlainText().contains("first = 9;"));
    projectView->closeCurrentFile();
    assert(projectView->count() == 1);
    editor.openNewScript(adoptedPath);
    assert(projectView->count() == 2);
    assert(projectView->getCurrentFile()->path == adoptedPath);
    assert(codeEditor->toPlainText().contains("first = 10;"));

    const QString newDocumentPath = temporary.filePath("new-document.eel");
    QTimer::singleShot(0, [temporaryPath = temporary.path()] {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (widget->windowTitle() != QStringLiteral("Create new script"))
                continue;
            auto *directory = widget->findChild<QLineEdit *>("directory");
            auto *filename = widget->findChild<QLineEdit *>("filename");
            assert(directory && filename);
            directory->setText(temporaryPath);
            filename->setText(QStringLiteral("new-document"));
            assert(QMetaObject::invokeMethod(widget, "accept", Qt::DirectConnection));
            return;
        }
    });
    editor.newProject();
    assert(projectView->count() == 3);
    assert(projectView->getCurrentFile()->path == newDocumentPath);
    editDocument(codeEditor, "@init\nnewValue = 42;\n@sample\nspl0 = spl0;\n");
    editor.runCode();
    assert(executions.count() == 2);
    assert(executions.constLast().at(0).toString() == newDocumentPath);
    assert(!codeEditor->document()->isModified());
    assert(readBytes(newDocumentPath).contains("newValue = 42;"));

    return 0;
}
