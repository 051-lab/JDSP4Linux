#include <cassert>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include "widgets/codeeditor.h"
#include "widgets/projectview.h"

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    CodeContainer first;
    first.code = "first";
    CodeContainer second;
    second.code = "second";

    CodeEditor editor;
    editor.loadCode(&first);
    editor.setPlainText("first edit");
    QCoreApplication::processEvents();
    assert(first.code == "first edit");

    editor.loadCode(&second);
    editor.setPlainText("second edit");
    QCoreApplication::processEvents();
    assert(second.code == "second edit");
    assert(first.code == "first edit");

    QTemporaryDir temporary;
    assert(temporary.isValid());
    const QString firstPath = temporary.filePath("first.eel");
    const QString secondPath = temporary.filePath("second.eel");
    QFile firstFile(firstPath);
    assert(firstFile.open(QIODevice::WriteOnly));
    assert(firstFile.write("@sample\nspl0 = spl0;\n") > 0);
    firstFile.close();
    QFile secondFile(secondPath);
    assert(secondFile.open(QIODevice::WriteOnly));
    assert(secondFile.write("@sample\nspl1 = spl1;\n") > 0);
    secondFile.close();

    ProjectView projects;
    projects.addFile(firstPath);
    assert(projects.getCurrentFile() != nullptr);
    projects.addFile(secondPath);
    assert(projects.getCurrentFile() != nullptr);
    projects.closeFile(firstPath);
    projects.closeCurrentFile();
    assert(projects.getCurrentFile() == nullptr);
    return 0;
}
