#include <cassert>
#include <archive.h>
#include <archive_entry.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <atomic>

#include "AeqPackageManager.h"
#include "GzipDownloader.h"
#include "GzipDownloaderDialog.h"

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

void makePackage(const QString& root)
{
    writeJson(QDir(root).filePath("version.json"),
              {QJsonObject{{"package_url", "https://example.invalid/package.tar.gz"}}});
    writeJson(QDir(root).filePath("index.json"),
              {QJsonObject{{"n", "headphone"}, {"s", "source"}, {"r", 1}}});
    const QString measurement = QDir(root).filePath("headphone/source");
    assert(QDir().mkpath(measurement));
    writeFile(QDir(measurement).filePath("raw.csv"), "frequency, gain\n");
}

QByteArray makeArchive(bool complete, bool corruptJson = false)
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

    const QByteArray version = corruptJson ? "{not-json" : "[{\"package_url\":\"http://127.0.0.1/fixture.tar.gz\"}]";
    archive_entry* versionEntry = archive_entry_new();
    assert(versionEntry);
    archive_entry_set_pathname(versionEntry, "version.json");
    archive_entry_set_size(versionEntry, version.size());
    archive_entry_set_filetype(versionEntry, AE_IFREG);
    archive_entry_set_perm(versionEntry, 0600);
    assert(archive_write_header(writer, versionEntry) == ARCHIVE_OK);
    assert(archive_write_data(writer, version.constData(), static_cast<size_t>(version.size())) == version.size());
    archive_entry_free(versionEntry);

    const QByteArray index = "[{\"n\":\"headphone\",\"s\":\"source\",\"r\":1}]";
    archive_entry* indexEntry = archive_entry_new();
    assert(indexEntry);
    archive_entry_set_pathname(indexEntry, "index.json");
    archive_entry_set_size(indexEntry, index.size());
    archive_entry_set_filetype(indexEntry, AE_IFREG);
    archive_entry_set_perm(indexEntry, 0600);
    assert(archive_write_header(writer, indexEntry) == ARCHIVE_OK);
    assert(archive_write_data(writer, index.constData(), static_cast<size_t>(index.size())) == index.size());
    archive_entry_free(indexEntry);

    if(complete)
    {
        const QByteArray payload = "frequency, gain\n";
        archive_entry* dataEntry = archive_entry_new();
        assert(dataEntry);
        archive_entry_set_pathname(dataEntry, "headphone/source/raw.csv");
        archive_entry_set_size(dataEntry, payload.size());
        archive_entry_set_filetype(dataEntry, AE_IFREG);
        archive_entry_set_perm(dataEntry, 0600);
        assert(archive_write_header(writer, dataEntry) == ARCHIVE_OK);
        assert(archive_write_data(writer, payload.constData(), static_cast<size_t>(payload.size())) == payload.size());
        archive_entry_free(dataEntry);
    }
    assert(archive_write_close(writer) == ARCHIVE_OK);
    assert(archive_write_free(writer) == ARCHIVE_OK);
    QFile archiveFile(path);
    assert(archiveFile.open(QIODevice::ReadOnly));
    return archiveFile.readAll();
}

class FixtureServer final : public QTcpServer
{
public:
    explicit FixtureServer(QObject* parent = nullptr) : QTcpServer(parent) {}

    void setResponse(const QByteArray& body)
    {
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/gzip\r\nContent-Length: " +
                   QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto* socket = new QTcpSocket(this);
        assert(socket->setSocketDescriptor(descriptor));
        socket->write(response);
        socket->disconnectFromHost();
    }

private:
    QByteArray response;
};
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    int validationMessages = 0;
    QTimer closeValidationMessages;
    QObject::connect(&closeValidationMessages, &QTimer::timeout, [&]() {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            ++validationMessages;
            message->accept();
        }
    });
    closeValidationMessages.start(1);
    QTemporaryDir temporary;
    assert(temporary.isValid());
    makePackage(temporary.path());

    AeqPackageManager manager(nullptr, temporary.path());
    assert(manager.databaseDirectory() == temporary.path());
    assert(manager.isPackageInstalled());

    QFile::remove(QDir(temporary.path()).filePath("headphone/source/raw.csv"));
    assert(!manager.isPackageInstalled());

    FixtureServer server;
    assert(server.listen(QHostAddress::LocalHost));
    AeqVersion remote;
    remote.packageUrl = QStringLiteral("http://127.0.0.1:%1/fixture.tar.gz").arg(server.serverPort());

    server.setResponse(makeArchive(true));
    bool installed = false;
    bool installRejected = false;
    manager.installPackage(remote).then([&]() { installed = true; }).fail([&]() { installRejected = true; });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    assert(installed);
    assert(!installRejected);
    assert(manager.isPackageInstalled());

    QFile previous(QDir(temporary.path()).filePath("headphone/source/raw.csv"));
    assert(previous.open(QIODevice::ReadOnly));
    const QByteArray previousContent = previous.readAll();
    server.setResponse(makeArchive(false));
    installed = false;
    installRejected = false;
    manager.installPackage(remote).then([&]() { installed = true; }).fail([&]() { installRejected = true; });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    assert(!installed);
    assert(installRejected);
    assert(manager.isPackageInstalled());
    QFile preserved(QDir(temporary.path()).filePath("headphone/source/raw.csv"));
    assert(preserved.open(QIODevice::ReadOnly));
    assert(preserved.readAll() == previousContent);

    /* Corrupt metadata must reject exactly once and preserve the prior package. */
    int corruptResolved = 0;
    int corruptRejected = 0;
    server.setResponse(makeArchive(true, true));
    manager.installPackage(remote).then([&]() { ++corruptResolved; }).fail([&]() { ++corruptRejected; });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    assert(corruptResolved == 0);
    assert(corruptRejected == 1);
    assert(validationMessages >= 2);
    assert(manager.isPackageInstalled());
    QFile preservedAfterCorrupt(QDir(temporary.path()).filePath("headphone/source/raw.csv"));
    assert(preservedAfterCorrupt.open(QIODevice::ReadOnly));
    assert(preservedAfterCorrupt.readAll() == previousContent);

    /* A destination occupied by a regular file is a filesystem publication failure. */
    const QString blockedParent = temporary.path() + "/blocked-parent";
    const QString blockedPath = blockedParent + "/database";
    writeFile(blockedParent, "keep me");
    AeqPackageManager blockedManager(nullptr, blockedPath);
    int blockedResolved = 0;
    int blockedRejected = 0;
    server.setResponse(makeArchive(true));
    blockedManager.installPackage(remote).then([&]() { ++blockedResolved; }).fail([&]() { ++blockedRejected; });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    assert(blockedResolved == 0);
    assert(blockedRejected == 1);
    assert(QFile(blockedParent).exists());
    assert(QFile(blockedParent).size() == 7);

    /* Closing the host while the manager's modal downloader extracts must reject safely. */
    QTemporaryDir shutdownDatabase;
    assert(shutdownDatabase.isValid());
    makePackage(shutdownDatabase.path());
    QWidget* shutdownHostRaw = new QWidget;
    QPointer<QWidget> shutdownHost = shutdownHostRaw;
    QPointer<AeqPackageManager> shutdownManager =
        new AeqPackageManager(shutdownHostRaw, shutdownDatabase.path());
    server.setResponse(makeArchive(true));
    bool shutdownResolved = false;
    bool shutdownRejected = false;
    std::atomic_bool shutdownHooked{false};
    std::atomic_bool shutdownTriggered{false};
    QTimer hookExtraction;
    QObject::connect(&hookExtraction, &QTimer::timeout, &app, [&]() {
        if(shutdownHooked.load())
            return;
        for(QWidget* window : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<GzipDownloaderDialog*>(window);
            if(!dialog)
                continue;
            GzipDownloader* downloader = dialog->findChild<GzipDownloader*>();
            if(!downloader)
                continue;
            shutdownHooked.store(true);
            QObject::connect(downloader, &GzipDownloader::decompressionStarted,
                             &app, [&]() {
                shutdownTriggered.store(true);
                if(shutdownHost)
                    shutdownHost->deleteLater();
            });
            hookExtraction.stop();
            break;
        }
    });
    hookExtraction.start(1);
    shutdownManager->installPackage(remote, shutdownHostRaw)
        .then([&]() { shutdownResolved = true; })
        .fail([&]() { shutdownRejected = true; });
    for(int i = 0; i < 300 && shutdownHost; ++i)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    hookExtraction.stop();
    for(int i = 0; i < 200 && !shutdownResolved && !shutdownRejected; ++i)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    assert(shutdownHooked.load());
    assert(shutdownTriggered.load());
    assert(shutdownHost.isNull());
    assert(shutdownManager.isNull());
    assert(!shutdownResolved);
    assert(shutdownRejected);
    AeqPackageManager verifyShutdownDatabase(nullptr, shutdownDatabase.path());
    assert(verifyShutdownDatabase.isPackageInstalled());
    QFile shutdownPreserved(QDir(shutdownDatabase.path()).filePath("headphone/source/raw.csv"));
    assert(shutdownPreserved.open(QIODevice::ReadOnly));
    assert(shutdownPreserved.readAll() == previousContent);
    return 0;
}
