#include <cassert>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "../subprojects/EELEditor/src/model/codecontainer.h"

static QByteArray bytes(const QString& path)
{
	QFile file(path);
	assert(file.open(QIODevice::ReadOnly));
	return file.readAll();
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	assert(temp.isValid());
	const QString path = temp.path() + "/script-é.eel";
	QFile initial(path);
	assert(initial.open(QIODevice::WriteOnly));
	initial.write("old source\n");
	initial.close();

	CodeContainer container(path);
	container.code = "new source\n";
	assert(container.save());
	assert(bytes(path) == QByteArray("new source\n"));

    container.code = "unsaved source\n";
    assert(!container.save(temp.path() + "/missing/destination.eel"));
    assert(bytes(path) == QByteArray("new source\n"));
    assert(container.path == path);
    assert(container.code == "unsaved source\n");

    container.code = "commit failure source\n";
    CodeContainerSetSaveCommitFailureForTests(true);
    assert(!container.save());
    CodeContainerSetSaveCommitFailureForTests(false);
    assert(bytes(path) == QByteArray("new source\n"));
    assert(container.path == path);
    assert(container.code == "commit failure source\n");
    return 0;
}
