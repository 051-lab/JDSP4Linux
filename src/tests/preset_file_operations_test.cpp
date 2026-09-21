#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cassert>

#include "data/SafeFileOperations.h"

static QByteArray read_file(const QString& path)
{
	QFile file(path);
	assert(file.open(QIODevice::ReadOnly));
	return file.readAll();
}

int main()
{
	QTemporaryDir temp;
	assert(temp.isValid());
	const QString source = temp.path() + "/source.conf";
	const QString destination = temp.path() + "/destination.conf";
	QFile sourceFile(source);
	assert(sourceFile.open(QIODevice::WriteOnly));
	assert(sourceFile.write("original configuration\n") > 0);
	sourceFile.close();

	const QByteArray original = read_file(source);
	QString error;
	assert(SafeFileOperations::copyAtomically(source, source, &error));
	assert(read_file(source) == original);

	QFile destinationFile(destination);
	assert(destinationFile.open(QIODevice::WriteOnly));
	destinationFile.write("old destination\n");
	destinationFile.close();
	assert(SafeFileOperations::copyAtomically(source, destination, &error));
	assert(read_file(destination) == original);
	SafeFileOperations::setCopyFailureForTests(true);
	assert(!SafeFileOperations::copyAtomically(source, destination, &error));
	assert(read_file(destination) == original);

	const QString hardlink = temp.path() + "/hardlink.conf";
	assert(QFile::link(source, hardlink));
	assert(SafeFileOperations::copyAtomically(source, hardlink, &error));
	assert(read_file(source) == original);

	const QString symlink = temp.path() + "/alias.conf";
	assert(QFile::link(source, symlink));
	assert(SafeFileOperations::copyAtomically(source, symlink, &error));
	assert(read_file(source) == original);

	const QString missing = temp.path() + "/missing.conf";
	assert(!SafeFileOperations::copyAtomically(missing, destination, &error));
	assert(!error.isEmpty());
	assert(read_file(destination) == original);

	assert(!SafeFileOperations::copyAtomically(source, temp.path() + "/no-such-dir/destination.conf", &error));
	assert(read_file(source) == original);
	assert(SafeFileOperations::isSafeName("movie"));
	assert(SafeFileOperations::isSafeName("my preset"));
	assert(!SafeFileOperations::isSafeName(""));
	assert(!SafeFileOperations::isSafeName("."));
	assert(!SafeFileOperations::isSafeName(".."));
	assert(!SafeFileOperations::isSafeName("../outside"));
	assert(!SafeFileOperations::isSafeName("sub/path"));
	assert(!SafeFileOperations::isSafeName("sub\\path"));

	const QString renameDirectory = temp.path() + "/rename";
	assert(QDir().mkpath(renameDirectory));
	const QDir renameDir(renameDirectory);
	QFile renameSource(renameDir.filePath("source.conf"));
	assert(renameSource.open(QIODevice::WriteOnly));
	assert(renameSource.write("rename source\n") > 0);
	renameSource.close();
	assert(SafeFileOperations::renameWithinDirectory(renameDir, "source.conf", "renamed.conf", &error));
	assert(!QFile::exists(renameDir.filePath("source.conf")));
	assert(read_file(renameDir.filePath("renamed.conf")) == "rename source\n");
	assert(SafeFileOperations::renameWithinDirectory(renameDir, "renamed.conf", "renamed.conf", &error));

	QFile occupied(renameDir.filePath("occupied.conf"));
	assert(occupied.open(QIODevice::WriteOnly));
	assert(occupied.write("occupied\n") > 0);
	occupied.close();
	assert(!SafeFileOperations::renameWithinDirectory(renameDir, "renamed.conf", "occupied.conf", &error));
	assert(read_file(renameDir.filePath("renamed.conf")) == "rename source\n");
	assert(read_file(renameDir.filePath("occupied.conf")) == "occupied\n");
	SafeFileOperations::setRenameFailureForTests(true);
	assert(!SafeFileOperations::renameWithinDirectory(renameDir, "renamed.conf", "new-name.conf", &error));
	assert(read_file(renameDir.filePath("renamed.conf")) == "rename source\n");
	assert(!QFile::exists(renameDir.filePath("new-name.conf")));
	SafeFileOperations::setRemoveFailureForTests(true);
	assert(!SafeFileOperations::removeWithinDirectory(renameDir, "renamed.conf", &error));
	assert(read_file(renameDir.filePath("renamed.conf")) == "rename source\n");
	assert(!SafeFileOperations::renameWithinDirectory(renameDir, "missing.conf", "new.conf", &error));
	assert(!SafeFileOperations::renameWithinDirectory(renameDir, "renamed.conf", "../outside.conf", &error));
	return 0;
}
