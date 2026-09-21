#include "SafeFileOperations.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <sys/stat.h>

namespace
{
void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

bool sameFile(const QString& first, const QString& second)
{
	struct stat firstStat = {};
	struct stat secondStat = {};
	return stat(first.toLocal8Bit().constData(), &firstStat) == 0 &&
		stat(second.toLocal8Bit().constData(), &secondStat) == 0 &&
		firstStat.st_dev == secondStat.st_dev && firstStat.st_ino == secondStat.st_ino;
}

#ifdef JDSP_TEST_HOOKS
bool copyFailureForTests = false;
bool renameFailureForTests = false;
bool removeFailureForTests = false;
#endif
}

namespace SafeFileOperations
{
bool copyAtomically(const QString& source, const QString& destination, QString* error)
{
	QFile input(source);
	if (!input.open(QIODevice::ReadOnly))
	{
		setError(error, QStringLiteral("Cannot read source '%1': %2").arg(source, input.errorString()));
		return false;
	}
	if (sameFile(source, destination))
		return true;

	const QByteArray contents = input.readAll();
	if (input.error() != QFileDevice::NoError)
	{
		setError(error, QStringLiteral("Cannot read source '%1': %2").arg(source, input.errorString()));
		return false;
	}
	input.close();

	QSaveFile output(destination);
	output.setDirectWriteFallback(false);
#ifdef JDSP_TEST_HOOKS
	if(copyFailureForTests)
	{
		copyFailureForTests = false;
		setError(error, QStringLiteral("Injected copy failure"));
		return false;
	}
#endif
	if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size())
	{
		setError(error, QStringLiteral("Cannot write destination '%1': %2").arg(destination, output.errorString()));
		return false;
	}
	if (!output.commit())
	{
		setError(error, QStringLiteral("Cannot commit destination '%1': %2").arg(destination, output.errorString()));
		return false;
	}
	return true;
}

bool isSafeName(const QString& name)
{
	return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..") &&
		!name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'));
}

bool renameWithinDirectory(const QDir& directory, const QString& sourceName,
                           const QString& destinationName, QString* error)
{
	if(!isSafeName(sourceName) || !isSafeName(destinationName))
	{
		setError(error, QStringLiteral("Invalid rename name"));
		return false;
	}
	const QString source = directory.filePath(sourceName);
	const QString destination = directory.filePath(destinationName);
	if(QFileInfo(source).absoluteFilePath() == QFileInfo(destination).absoluteFilePath())
		return true;
	if(!QFileInfo::exists(source))
	{
		setError(error, QStringLiteral("Rename source does not exist"));
		return false;
	}
	if(QFileInfo::exists(destination))
	{
		setError(error, QStringLiteral("Rename destination already exists"));
		return false;
	}
#ifdef JDSP_TEST_HOOKS
	if(renameFailureForTests)
	{
		renameFailureForTests = false;
		setError(error, QStringLiteral("Injected rename failure"));
		return false;
	}
#endif
	if(!QFile::rename(source, destination))
	{
		setError(error, QStringLiteral("Cannot rename '%1' to '%2'").arg(sourceName, destinationName));
		return false;
	}
	return true;
}

bool removeWithinDirectory(const QDir& directory, const QString& name, QString* error)
{
	if(!isSafeName(name))
	{
		setError(error, QStringLiteral("Invalid remove name"));
		return false;
	}
	const QString path = directory.filePath(name);
	if(!QFileInfo::exists(path))
	{
		setError(error, QStringLiteral("Remove source does not exist"));
		return false;
	}
#ifdef JDSP_TEST_HOOKS
	if(removeFailureForTests)
	{
		removeFailureForTests = false;
		setError(error, QStringLiteral("Injected remove failure"));
		return false;
	}
#endif
	if(!QFile::remove(path))
	{
		setError(error, QStringLiteral("Cannot remove '%1'").arg(name));
		return false;
	}
	return true;
}

#ifdef JDSP_TEST_HOOKS
void setCopyFailureForTests(bool fail)
{
	copyFailureForTests = fail;
}

void setRenameFailureForTests(bool fail)
{
	renameFailureForTests = fail;
}

void setRemoveFailureForTests(bool fail)
{
	removeFailureForTests = fail;
}
#endif
}
