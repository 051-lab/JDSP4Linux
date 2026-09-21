#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <archive.h>
#include <archive_entry.h>

#include <cassert>

#include "Untar.h"

static QString create_archive(const QString& root, const QString& archive)
{
	QFile payload(root + "/payload.txt");
	assert(payload.open(QIODevice::WriteOnly));
	payload.write("archive payload\n");
	payload.close();

	QProcess tar;
	tar.start("tar", {"-czf", archive, "-C", root, "payload.txt"});
	assert(tar.waitForFinished());
	assert(tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0);
	return archive;
}

static QString create_unsafe_archive(const QString& root, const QString& archive)
{
	QFile payload(root + "/safe.txt");
	assert(payload.open(QIODevice::WriteOnly));
	payload.write("must not escape\n");
	payload.close();

	QProcess tar;
	tar.start("tar", {"-czf", archive, "--transform=s|safe.txt|../escape.txt|", "-C", root, "safe.txt"});
	assert(tar.waitForFinished());
	assert(tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0);
	return archive;
}

static QString create_absolute_archive(const QString& root, const QString& archive)
{
	QFile payload(root + "/absolute-safe.txt");
	assert(payload.open(QIODevice::WriteOnly));
	payload.write("must not escape\n");
	payload.close();

	QProcess tar;
	tar.start("tar", {"-czf", archive, "--transform=s|absolute-safe.txt|/absolute-escape.txt|", "-C", root, "absolute-safe.txt"});
	assert(tar.waitForFinished());
	assert(tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0);
	return archive;
}

static QString create_duplicate_archive(const QString& root, const QString& archive)
{
	Q_UNUSED(root);
	struct archive* writer = archive_write_new();
	assert(writer);
	assert(archive_write_add_filter_gzip(writer) == ARCHIVE_OK);
	assert(archive_write_set_format_pax_restricted(writer) == ARCHIVE_OK);
	assert(archive_write_open_filename(writer, archive.toUtf8().constData()) == ARCHIVE_OK);
	const QByteArray payload("duplicate payload\n");
	for (int copy = 0; copy < 2; ++copy)
	{
		struct archive_entry* entry = archive_entry_new();
		assert(entry);
		archive_entry_set_pathname(entry, "payload.txt");
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_entry_set_size(entry, payload.size());
		assert(archive_write_header(writer, entry) == ARCHIVE_OK);
		assert(archive_write_data(writer, payload.constData(), payload.size()) == payload.size());
		assert(archive_write_finish_entry(writer) == ARCHIVE_OK);
		archive_entry_free(entry);
	}
	assert(archive_write_close(writer) == ARCHIVE_OK);
	assert(archive_write_free(writer) == ARCHIVE_OK);
	return archive;
}

static QString create_oversized_archive(const QString& root, const QString& archive)
{
	QProcess dd;
	dd.start("dd", {"if=/dev/zero", "of=" + root + "/oversized.bin", "bs=1M", "count=65", "status=none"});
	assert(dd.waitForFinished());
	assert(dd.exitStatus() == QProcess::NormalExit && dd.exitCode() == 0);
	QProcess tar;
	tar.start("tar", {"-czf", archive, "-C", root, "oversized.bin"});
	assert(tar.waitForFinished());
	assert(tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0);
	return archive;
}

static QString create_link_archive(const QString& archive, bool hardlink)
{
	struct archive* writer = archive_write_new();
	assert(writer);
	assert(archive_write_add_filter_gzip(writer) == ARCHIVE_OK);
	assert(archive_write_set_format_pax_restricted(writer) == ARCHIVE_OK);
	assert(archive_write_open_filename(writer, archive.toUtf8().constData()) == ARCHIVE_OK);
	struct archive_entry* entry = archive_entry_new();
	assert(entry);
	archive_entry_set_pathname(entry, hardlink ? "hardlink.txt" : "symlink.txt");
	if (hardlink)
	{
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_hardlink(entry, "../outside.txt");
	}
	else
	{
		archive_entry_set_filetype(entry, AE_IFLNK);
		archive_entry_set_symlink(entry, "../outside.txt");
	}
	archive_entry_set_perm(entry, 0644);
	archive_entry_set_size(entry, 0);
	assert(archive_write_header(writer, entry) == ARCHIVE_OK);
	assert(archive_write_finish_entry(writer) == ARCHIVE_OK);
	archive_entry_free(entry);
	assert(archive_write_close(writer) == ARCHIVE_OK);
	assert(archive_write_free(writer) == ARCHIVE_OK);
	return archive;
}

static QString create_fifo_archive(const QString& archive)
{
	struct archive* writer = archive_write_new();
	assert(writer);
	assert(archive_write_add_filter_gzip(writer) == ARCHIVE_OK);
	assert(archive_write_set_format_pax_restricted(writer) == ARCHIVE_OK);
	assert(archive_write_open_filename(writer, archive.toUtf8().constData()) == ARCHIVE_OK);
	struct archive_entry* entry = archive_entry_new();
	assert(entry);
	archive_entry_set_pathname(entry, "named-pipe");
	archive_entry_set_filetype(entry, AE_IFIFO);
	archive_entry_set_perm(entry, 0644);
	archive_entry_set_size(entry, 0);
	assert(archive_write_header(writer, entry) == ARCHIVE_OK);
	assert(archive_write_finish_entry(writer) == ARCHIVE_OK);
	archive_entry_free(entry);
	assert(archive_write_close(writer) == ARCHIVE_OK);
	assert(archive_write_free(writer) == ARCHIVE_OK);
	return archive;
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	assert(temp.isValid());

	const QString archive = temp.path() + "/archive.tar.gz";
	create_archive(temp.path(), archive);

	QString error;
	const QString output = temp.path() + "/output";
	assert(QDir().mkpath(output));
	assert(Untar::extract(archive, QDir(output), error) == 0);
	QFile extracted(output + "/payload.txt");
	assert(extracted.open(QIODevice::ReadOnly));
	assert(extracted.readAll() == QByteArray("archive payload\n"));

	const QString invalidOutput = temp.path() + "/output-file";
	QFile invalidOutputFile(invalidOutput);
	assert(invalidOutputFile.open(QIODevice::WriteOnly));
	invalidOutputFile.close();
	error.clear();
	assert(Untar::extract(archive, QDir(invalidOutput), error) != 0);
	assert(!error.isEmpty());
	assert(!QFile::exists(invalidOutput + "/payload.txt"));

	const QString cancelledOutput = temp.path() + "/cancelled-output";
	assert(QDir().mkpath(cancelledOutput));
	int interruptionChecks = 0;
	error.clear();
	assert(Untar::extract(archive, QDir(cancelledOutput), error, [&]() {
		return ++interruptionChecks > 1;
	}) != 0);
	assert(error == QStringLiteral("Extraction cancelled"));
	assert(interruptionChecks >= 2);

	const QString unicodeArchive = temp.path() + "/архив-é.tar.gz";
	assert(QFile::copy(archive, unicodeArchive));
	const QString unicodeOutput = temp.path() + "/unicode-output";
	assert(QDir().mkpath(unicodeOutput));
	error.clear();
	assert(Untar::extract(unicodeArchive, QDir(unicodeOutput), error) == 0);

	const QString truncatedArchive = temp.path() + "/truncated.tar.gz";
	assert(QFile::copy(archive, truncatedArchive));
	QFile truncatedFile(truncatedArchive);
	assert(truncatedFile.open(QIODevice::ReadWrite));
	const qint64 truncatedSize = truncatedFile.size() / 2;
	assert(truncatedSize > 0);
	assert(truncatedFile.resize(truncatedSize));
	truncatedFile.close();
	const QString truncatedOutput = temp.path() + "/truncated-output";
	assert(QDir().mkpath(truncatedOutput));
	error.clear();
	assert(Untar::extract(truncatedArchive, QDir(truncatedOutput), error) != 0);
	assert(!error.isEmpty());

	error.clear();
	assert(Untar::extract(temp.path() + "/missing.tar.gz", QDir(output), error) != 0);
	assert(!error.isEmpty());

	const QString unsafeArchive = temp.path() + "/unsafe.tar.gz";
	create_unsafe_archive(temp.path(), unsafeArchive);
	const QString unsafeOutput = temp.path() + "/unsafe-output";
	assert(QDir().mkpath(unsafeOutput));
	error.clear();
	assert(Untar::extract(unsafeArchive, QDir(unsafeOutput), error) != 0);
	assert(!QFile::exists(temp.path() + "/escape.txt"));

	const QString absoluteArchive = temp.path() + "/absolute.tar.gz";
	create_absolute_archive(temp.path(), absoluteArchive);
	const QString absoluteOutput = temp.path() + "/absolute-output";
	assert(QDir().mkpath(absoluteOutput));
	error.clear();
	assert(Untar::extract(absoluteArchive, QDir(absoluteOutput), error) != 0);
	assert(!QFile::exists("/absolute-escape.txt"));

	const QString duplicateArchive = temp.path() + "/duplicate.tar.gz";
	create_duplicate_archive(temp.path(), duplicateArchive);
	const QString duplicateOutput = temp.path() + "/duplicate-output";
	assert(QDir().mkpath(duplicateOutput));
	error.clear();
	assert(Untar::extract(duplicateArchive, QDir(duplicateOutput), error) != 0);
	assert(error.contains("duplicate"));

	const QString oversizedArchive = temp.path() + "/oversized.tar.gz";
	create_oversized_archive(temp.path(), oversizedArchive);
	const QString oversizedOutput = temp.path() + "/oversized-output";
	assert(QDir().mkpath(oversizedOutput));
	error.clear();
	assert(Untar::extract(oversizedArchive, QDir(oversizedOutput), error) != 0);
	assert(error.contains("size limits"));

	for (bool hardlink : {false, true})
	{
		const QString linkArchive = temp.path() + (hardlink ? "/hardlink.tar.gz" : "/symlink.tar.gz");
		create_link_archive(linkArchive, hardlink);
		const QString linkOutput = temp.path() + (hardlink ? "/hardlink-output" : "/symlink-output");
		assert(QDir().mkpath(linkOutput));
		error.clear();
		assert(Untar::extract(linkArchive, QDir(linkOutput), error) != 0);
		assert(error == QStringLiteral("Archive contains an unsafe member"));
		assert(!QFile::exists(temp.path() + "/outside.txt"));
	}

	const QString fifoArchive = temp.path() + "/fifo.tar.gz";
	create_fifo_archive(fifoArchive);
	const QString fifoOutput = temp.path() + "/fifo-output";
	assert(QDir().mkpath(fifoOutput));
	error.clear();
	assert(Untar::extract(fifoArchive, QDir(fifoOutput), error) != 0);
	assert(error == QStringLiteral("Archive contains an unsafe member"));
	assert(!QFile::exists(fifoOutput + "/named-pipe"));
	return 0;
}
