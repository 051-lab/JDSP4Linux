#include <QApplication>
#include <QNetworkReply>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include <atomic>
#include <cassert>
#include <cstring>
#include <archive.h>
#include <archive_entry.h>

#include "GzipDownloader.h"
#include "GzipDownloaderDialog.h"
#include "ExtractionThread.h"

class FixtureReply final : public QNetworkReply
{
public:
	FixtureReply()
	{
		setUrl(QUrl("https://example.invalid/package.tar.gz"));
		open(QIODevice::ReadOnly);
	}

	void abort() override { aborted = true; }
	bool isSequential() const override { return true; }
	void setLength(qint64 length) { setHeader(QNetworkRequest::ContentLengthHeader, length); }
	void setData(const QByteArray& value, bool advertiseLength = true)
	{
		data = value;
		position = 0;
		available = 0;
		if(advertiseLength)
			setLength(data.size());
	}
	void feedChunk(qint64 bytes)
	{
		available = qMin(data.size(), available + qMax<qint64>(0, bytes));
		emit readyRead();
	}
	void finish()
	{
		available = data.size();
		emit readyRead();
		emit finished();
	}
	void feed() { available = data.size(); finish(); }

	bool aborted = false;

protected:
	qint64 readData(char* buffer, qint64 maxSize) override
	{
		const qint64 readable = available - position;
		const qint64 count = qMin(maxSize, readable);
		if(count > 0)
			std::memcpy(buffer, data.constData() + position, static_cast<size_t>(count));
		position += count;
		return count;
	}
	qint64 writeData(const char*, qint64) override { return -1; }

	private:
	QByteArray data;
	qint64 position = 0;
	qint64 available = 0;
};

QByteArray makeArchive()
{
	QTemporaryDir temporary;
	assert(temporary.isValid());
	const QString path = temporary.filePath("fixture.tar.gz");
	archive* writer = archive_write_new();
	assert(writer);
	assert(archive_write_add_filter_gzip(writer) == ARCHIVE_OK);
	assert(archive_write_set_format_pax_restricted(writer) == ARCHIVE_OK);
	const QByteArray filename = path.toUtf8();
	assert(archive_write_open_filename(writer, filename.constData()) == ARCHIVE_OK);
	archive_entry* entry = archive_entry_new();
	assert(entry);
	archive_entry_set_pathname(entry, "large.bin");
	const qint64 size = 16LL * 1024LL * 1024LL;
	archive_entry_set_size(entry, size);
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, 0600);
	assert(archive_write_header(writer, entry) == ARCHIVE_OK);
	QByteArray payload(static_cast<qsizetype>(size), '\0');
	for(qsizetype i = 0; i < payload.size(); i += 4096)
		payload[i] = static_cast<char>(i / 4096);
	assert(archive_write_data(writer, payload.constData(), static_cast<size_t>(payload.size())) == size);
	archive_entry_free(entry);
	assert(archive_write_close(writer) == ARCHIVE_OK);
	assert(archive_write_free(writer) == ARCHIVE_OK);
	QFile archiveFile(path);
	assert(archiveFile.open(QIODevice::ReadOnly));
	return archiveFile.readAll();
}

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	GzipDownloader downloader;
	FixtureReply* reply = new FixtureReply;
	reply->setLength(128LL * 1024LL * 1024LL + 1);
	QSignalSpy errors(&downloader, &GzipDownloader::errorOccurred);

	assert(!downloader.start(reply, QDir("/tmp")));
	assert(reply->aborted);
	QCoreApplication::sendPostedEvents();
	assert(errors.count() == 1);
	assert(errors.at(0).at(0).toString().contains("size limit"));

	assert(!downloader.start(nullptr, QDir("/tmp")));
	assert(errors.count() == 1);

	FixtureReply* streamedReply = new FixtureReply;
	streamedReply->setData(QByteArray(128LL * 1024LL * 1024LL + 1, '\0'), false);
	assert(downloader.start(streamedReply, QDir("/tmp")));
	streamedReply->feed();
	QCoreApplication::sendPostedEvents();
	assert(errors.count() == 2);
	assert(errors.at(1).at(0).toString().contains("size limit"));

	FixtureReply* shortWriteReply = new FixtureReply;
	shortWriteReply->setData("not empty");
	downloader.setWriteFailureForTests(0);
	assert(downloader.start(shortWriteReply, QDir("/tmp")));
	shortWriteReply->feed();
	QCoreApplication::sendPostedEvents();
	assert(errors.count() == 3);
	assert(errors.at(2).at(0).toString().contains("cannot be written"));
	downloader.setWriteFailureForTests(-1);

	FixtureReply* downloadCancelReply = new FixtureReply;
	assert(downloader.start(downloadCancelReply, QDir("/tmp")));
	downloader.abort();
	downloader.abort();
	assert(downloadCancelReply->aborted);
	QCoreApplication::sendPostedEvents();
	assert(errors.count() == 3);

	FixtureReply* firstActiveReply = new FixtureReply;
	FixtureReply* repeatedReply = new FixtureReply;
	assert(downloader.start(firstActiveReply, QDir("/tmp")));
	assert(!downloader.start(repeatedReply, QDir("/tmp")));
	assert(errors.count() == 4);
	downloader.abort();
	QCoreApplication::sendPostedEvents();
	assert(firstActiveReply->aborted);
	repeatedReply->deleteLater();

	FixtureReply* activeReply = new FixtureReply;
	activeReply->setData(makeArchive());
	auto* activeDownloader = new GzipDownloader;
	QSignalSpy decompressionStarted(activeDownloader, &GzipDownloader::decompressionStarted);
	QSignalSpy activeErrors(activeDownloader, &GzipDownloader::errorOccurred);
	QSignalSpy activeSuccess(activeDownloader, &GzipDownloader::success);
	assert(activeDownloader->start(activeReply, QDir(QDir::tempPath())));
	activeReply->feed();
	for(int i = 0; i < 100 && decompressionStarted.isEmpty(); ++i)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	assert(decompressionStarted.count() == 1);
	activeDownloader->abort();
	for(int i = 0; i < 200 && activeErrors.isEmpty() && activeSuccess.isEmpty(); ++i)
		QTest::qWait(10);
	assert(activeErrors.count() == 1);
	assert(activeErrors.at(0).at(0).toString().contains("cancel"));
	assert(activeSuccess.isEmpty());
	delete activeDownloader;
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

	QTemporaryDir slowArchiveDirectory;
	QTemporaryDir slowExtractionDirectory;
	assert(slowArchiveDirectory.isValid() && slowExtractionDirectory.isValid());
	const QString slowArchivePath = slowArchiveDirectory.filePath("slow.tar.gz");
	const QByteArray slowArchive = makeArchive();
	QFile slowArchiveFile(slowArchivePath);
	assert(slowArchiveFile.open(QIODevice::WriteOnly));
	assert(slowArchiveFile.write(slowArchive) == slowArchive.size());
	slowArchiveFile.close();
	std::atomic<int> slowChecks{0};
	std::atomic_bool slowCancel{false};
	ExtractionThread slowWorker(slowExtractionDirectory.path(), slowArchivePath, nullptr, [&]() {
		++slowChecks;
		QThread::msleep(2);
		return slowCancel.load();
	});
	QSignalSpy slowFinished(&slowWorker, &ExtractionThread::onFinished);
	slowWorker.start();
	for(int i = 0; i < 200 && slowChecks.load() == 0; ++i)
		QTest::qWait(5);
	assert(slowChecks.load() > 0);
	slowCancel.store(true);
	assert(slowWorker.wait(5000));
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
	assert(slowFinished.count() == 1);
	assert(slowFinished.at(0).at(0).toString().contains("cancel"));

	QTemporaryDir chunkedExtraction;
	assert(chunkedExtraction.isValid());
	FixtureReply* chunkedReply = new FixtureReply;
	const QByteArray chunkedArchive = makeArchive();
	chunkedReply->setData(chunkedArchive);
	auto* chunkedDownloader = new GzipDownloader;
	QSignalSpy chunkedStarted(chunkedDownloader, &GzipDownloader::decompressionStarted);
	QSignalSpy chunkedErrors(chunkedDownloader, &GzipDownloader::errorOccurred);
	QSignalSpy chunkedSuccess(chunkedDownloader, &GzipDownloader::success);
	assert(chunkedDownloader->start(chunkedReply, QDir(chunkedExtraction.path())));
	for(qint64 sent = 0; sent < chunkedArchive.size(); sent += 65536)
	{
		chunkedReply->feedChunk(65536);
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	}
	chunkedReply->finish();
	for(int i = 0; i < 300 && chunkedSuccess.isEmpty() && chunkedErrors.isEmpty(); ++i)
		QTest::qWait(10);
	assert(chunkedStarted.count() == 1);
	assert(chunkedErrors.isEmpty());
	assert(chunkedSuccess.count() == 1);
	assert(QFileInfo::exists(chunkedExtraction.filePath("large.bin")));
	delete chunkedDownloader;
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

	{
		QWidget parent;
		QPointer<GzipDownloader> child = new GzipDownloader(&parent);
		FixtureReply* parentReply = new FixtureReply;
		parentReply->setData(makeArchive());
		QSignalSpy parentDecompressionStarted(child, &GzipDownloader::decompressionStarted);
		assert(child->start(parentReply, QDir(QDir::tempPath())));
		parentReply->feed();
		for(int i = 0; i < 100 && parentDecompressionStarted.isEmpty(); ++i)
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		assert(parentDecompressionStarted.count() == 1);
		/* QObject parent destruction must wait for the extraction worker safely. */
	}

	{
		QPointer<QWidget> parent = new QWidget;
		FixtureReply* parentDialogReply = new FixtureReply;
		parentDialogReply->setData(makeArchive());
		QPointer<GzipDownloaderDialog> parentDialog = new GzipDownloaderDialog(
			parentDialogReply, QDir(QDir::tempPath()), parent);
		GzipDownloader* parentDialogDownloader = parentDialog->findChild<GzipDownloader*>();
		QSignalSpy parentDialogStarted(parentDialogDownloader,
			&GzipDownloader::decompressionStarted);
		parentDialog->show();
		parentDialogReply->feed();
		for(int i = 0; i < 100 && parentDialogStarted.isEmpty(); ++i)
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		assert(parentDialogStarted.count() == 1);
		/* Destroying the dialog's parent must not delete an active worker. */
		parent->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		for(int i = 0; i < 300 && !parentDialog.isNull(); ++i)
		{
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		}
		assert(parentDialog.isNull());
	}

	FixtureReply* dialogReply = new FixtureReply;
	dialogReply->setData(makeArchive());
	QPointer<GzipDownloaderDialog> dialog = new GzipDownloaderDialog(
		dialogReply, QDir(QDir::tempPath()));
	GzipDownloader* dialogDownloader = dialog->findChild<GzipDownloader*>();
	assert(dialogDownloader);
	dialogDownloader->setExtractionInterruptionCheckForTests([&]() {
		QThread::msleep(2);
		return false;
	});
	QSignalSpy dialogDecompressionStarted(dialog->findChild<GzipDownloader*>(),
		&GzipDownloader::decompressionStarted);
	dialog->show();
	dialogReply->feed();
	for(int i = 0; i < 100 && dialogDecompressionStarted.isEmpty(); ++i)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	assert(dialogDecompressionStarted.count() == 1);
	/* Decompression has disabled closure; reject must not abandon the worker. */
	dialog->reject();
	assert(dialog->isVisible());
	dialog->close();
	assert(dialog->isVisible());
	for(int i = 0; i < 300 && dialog->isVisible(); ++i)
		QTest::qWait(10);
	assert(dialog->result() == QDialog::Accepted);
	dialog->deleteLater();
	for(int i = 0; i < 100 && !dialog.isNull(); ++i)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	assert(dialog.isNull());

	FixtureReply* dialogExtractionCancelReply = new FixtureReply;
	dialogExtractionCancelReply->setData(makeArchive());
	QPointer<GzipDownloaderDialog> extractionCancelledDialog = new GzipDownloaderDialog(
		dialogExtractionCancelReply, QDir(QDir::tempPath()));
	std::atomic_bool extractionCancel{false};
	GzipDownloader* extractionCancelDownloader = extractionCancelledDialog->findChild<GzipDownloader*>();
	assert(extractionCancelDownloader);
	extractionCancelDownloader->setExtractionInterruptionCheckForTests([&]() {
		QThread::msleep(2);
		return extractionCancel.load();
	});
	QSignalSpy extractionCancelStarted(extractionCancelDownloader,
		&GzipDownloader::decompressionStarted);
	extractionCancelledDialog->show();
	dialogExtractionCancelReply->feed();
	for(int i = 0; i < 100 && extractionCancelStarted.isEmpty(); ++i)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	assert(extractionCancelStarted.count() == 1);
	extractionCancel.store(true);
	for(int i = 0; i < 300 && extractionCancelledDialog->isVisible(); ++i)
		QTest::qWait(10);
	assert(extractionCancelledDialog->result() == QDialog::Rejected);
	extractionCancelledDialog->deleteLater();
	for(int i = 0; i < 100 && !extractionCancelledDialog.isNull(); ++i)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	assert(extractionCancelledDialog.isNull());

	FixtureReply* validationCancelReply = new FixtureReply;
	validationCancelReply->setData(makeArchive());
	std::atomic_bool validationEntered{false};
	std::atomic_bool validationObservedCancel{false};
	GzipDownloader::PackageValidator slowValidator = [&](const QString&, const std::function<bool()>& cancelled) {
		validationEntered.store(true);
		while(!cancelled())
			QThread::msleep(2);
		validationObservedCancel.store(true);
		return QStringLiteral("Package validation cancelled");
	};
	QPointer<GzipDownloaderDialog> validationCancelledDialog = new GzipDownloaderDialog(
		validationCancelReply, QDir(QDir::tempPath()), nullptr, std::move(slowValidator));
	GzipDownloader* validationCancelDownloader = validationCancelledDialog->findChild<GzipDownloader*>();
	assert(validationCancelDownloader);
	QSignalSpy validationStarted(validationCancelDownloader, &GzipDownloader::validationStarted);
	QSignalSpy validationSuccess(validationCancelDownloader, &GzipDownloader::success);
	validationCancelledDialog->show();
	validationCancelReply->feed();
	for(int i = 0; i < 300 && validationStarted.isEmpty(); ++i)
		QTest::qWait(10);
	assert(validationEntered.load());
	assert(validationStarted.count() == 1);
	auto* validationCancelButtons = validationCancelledDialog->findChild<QDialogButtonBox*>("buttonBox");
	assert(validationCancelButtons && validationCancelButtons->isEnabled());
	validationCancelButtons->button(QDialogButtonBox::Abort)->click();
	for(int i = 0; i < 300 && validationCancelledDialog->isVisible(); ++i)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	assert(validationCancelledDialog->result() == QDialog::Rejected);
	for(int i = 0; i < 100 && !validationObservedCancel.load(); ++i)
		QTest::qWait(5);
	assert(validationObservedCancel.load());
	assert(validationSuccess.isEmpty());
	validationCancelledDialog->deleteLater();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	assert(validationCancelledDialog.isNull());

	/* Cancellation racing the validator's successful return must win over publication. */
	FixtureReply* validationCompletionReply = new FixtureReply;
	validationCompletionReply->setData(makeArchive());
	std::atomic_bool completionCancellation{false};
	std::atomic_bool completionValidatorReturned{false};
	GzipDownloader::PackageValidator completionRaceValidator = [&](const QString&, const std::function<bool()>&) {
		completionCancellation.store(true);
		completionValidatorReturned.store(true);
		return QString();
	};
	QPointer<GzipDownloaderDialog> completionRaceDialog = new GzipDownloaderDialog(
		validationCompletionReply, QDir(QDir::tempPath()), nullptr, std::move(completionRaceValidator));
	GzipDownloader* completionRaceDownloader = completionRaceDialog->findChild<GzipDownloader*>();
	assert(completionRaceDownloader);
	completionRaceDownloader->setExtractionInterruptionCheckForTests([&]() {
		return completionCancellation.load();
	});
	QSignalSpy completionRaceSuccess(completionRaceDownloader, &GzipDownloader::success);
	QSignalSpy completionRaceErrors(completionRaceDownloader, &GzipDownloader::errorOccurred);
	completionRaceDialog->show();
	validationCompletionReply->feed();
	for(int i = 0; i < 300 && completionRaceDialog->isVisible(); ++i)
		QTest::qWait(10);
	assert(completionValidatorReturned.load());
	assert(completionRaceDialog->result() == QDialog::Rejected);
	assert(completionRaceSuccess.isEmpty());
	assert(completionRaceErrors.count() == 1);
	completionRaceDialog->deleteLater();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	assert(completionRaceDialog.isNull());

	/* Destroying the owning window during validation interrupts and drains its worker. */
	QPointer<QWidget> validationParent = new QWidget;
	FixtureReply* validationShutdownReply = new FixtureReply;
	validationShutdownReply->setData(makeArchive());
	std::atomic_bool shutdownValidatorEntered{false};
	std::atomic_bool shutdownValidatorObservedCancel{false};
	GzipDownloader::PackageValidator shutdownValidator = [&](const QString&, const std::function<bool()>& cancelled) {
		shutdownValidatorEntered.store(true);
		while(!cancelled())
			QThread::msleep(2);
		shutdownValidatorObservedCancel.store(true);
		return QStringLiteral("Package validation cancelled");
	};
	QPointer<GzipDownloaderDialog> validationShutdownDialog = new GzipDownloaderDialog(
		validationShutdownReply, QDir(QDir::tempPath()), validationParent, std::move(shutdownValidator));
	GzipDownloader* validationShutdownDownloader = validationShutdownDialog->findChild<GzipDownloader*>();
	assert(validationShutdownDownloader);
	QSignalSpy validationShutdownStarted(validationShutdownDownloader, &GzipDownloader::validationStarted);
	validationShutdownDialog->show();
	validationShutdownReply->feed();
	for(int i = 0; i < 300 && validationShutdownStarted.isEmpty(); ++i)
		QTest::qWait(10);
	assert(shutdownValidatorEntered.load());
	assert(validationShutdownStarted.count() == 1);
	validationParent->deleteLater();
	for(int i = 0; i < 300 && !validationParent.isNull(); ++i)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	assert(validationParent.isNull());
	assert(validationShutdownDialog.isNull());
	for(int i = 0; i < 100 && !shutdownValidatorObservedCancel.load(); ++i)
		QTest::qWait(5);
	assert(shutdownValidatorObservedCancel.load());

	FixtureReply* extractionUiCancelReply = new FixtureReply;
	extractionUiCancelReply->setData(makeArchive());
	std::atomic_bool extractionUiObservedCancel{false};
	QPointer<GzipDownloaderDialog> extractionUiCancelledDialog = new GzipDownloaderDialog(
		extractionUiCancelReply, QDir(QDir::tempPath()));
	GzipDownloader* extractionUiDownloader = extractionUiCancelledDialog->findChild<GzipDownloader*>();
	assert(extractionUiDownloader);
	extractionUiDownloader->setExtractionInterruptionCheckForTests([&]() {
		QThread::msleep(1);
		const bool interrupted = QThread::currentThread()->isInterruptionRequested();
		if(interrupted)
			extractionUiObservedCancel.store(true);
		return interrupted;
	});
	QSignalSpy extractionUiStarted(extractionUiDownloader, &GzipDownloader::decompressionStarted);
	extractionUiCancelledDialog->show();
	extractionUiCancelReply->feed();
	for(int i = 0; i < 100 && extractionUiStarted.isEmpty(); ++i)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	assert(extractionUiStarted.count() == 1);
	QTest::keyClick(extractionUiCancelledDialog, Qt::Key_Escape);
	for(int i = 0; i < 300 && extractionUiCancelledDialog->isVisible(); ++i)
		QTest::qWait(10);
	assert(extractionUiCancelledDialog->result() == QDialog::Rejected);
	assert(extractionUiObservedCancel.load());
	extractionUiCancelledDialog->deleteLater();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	assert(extractionUiCancelledDialog.isNull());

	FixtureReply* dialogCancelReply = new FixtureReply;
	QPointer<GzipDownloaderDialog> cancelledDialog = new GzipDownloaderDialog(
		dialogCancelReply, QDir(QDir::tempPath()));
	cancelledDialog->show();
	QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
	assert(!cancelledDialog.isNull());
	QDialogButtonBox* cancelButtons = cancelledDialog->findChild<QDialogButtonBox*>("buttonBox");
	assert(cancelButtons && cancelButtons->button(QDialogButtonBox::Abort));
	cancelButtons->button(QDialogButtonBox::Abort)->click();
	assert(cancelledDialog->result() == QDialog::Rejected);
	assert(dialogCancelReply->aborted);
	cancelledDialog->deleteLater();
	for(int i = 0; i < 100 && !cancelledDialog.isNull(); ++i)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	assert(cancelledDialog.isNull());

	FixtureReply* escapeReply = new FixtureReply;
	QPointer<GzipDownloaderDialog> escapeDialog = new GzipDownloaderDialog(
		escapeReply, QDir(QDir::tempPath()));
	escapeDialog->show();
	QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
	QTest::keyClick(escapeDialog, Qt::Key_Escape);
	assert(escapeDialog->result() == QDialog::Rejected);
	assert(escapeReply->aborted);
	escapeDialog->deleteLater();
	for(int i = 0; i < 100 && !escapeDialog.isNull(); ++i)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
	assert(escapeDialog.isNull());
	return 0;
}
