#include "GzipDownloader.h"

#include "Untar.h"
#include "ExtractionThread.h"

#ifdef JDSP_TEST_HOOKS
static qint64 writeFailureBytes = -1;
#endif

GzipDownloader::~GzipDownloader()
{
    cleanup();
    if(extractThread)
    {
        extractThread->wait();
        extractThread = nullptr;
    }
    downloadedFile.close();
    if(!downloadedFile.fileName().isEmpty())
        downloadedFile.remove();
}

#ifdef JDSP_TEST_HOOKS
void GzipDownloader::setWriteFailureForTests(qint64 bytesBeforeShortWrite)
{
    writeFailureBytes = bytesBeforeShortWrite;
}

void GzipDownloader::setExtractionInterruptionCheckForTests(std::function<bool()> check)
{
    extractionInterruptionCheck = std::move(check);
}
#endif

qint64 GzipDownloader::writeDownloadedData(const QByteArray& data)
{
#ifdef JDSP_TEST_HOOKS
    if(writeFailureBytes >= 0)
    {
        const qint64 writable = qMin(writeFailureBytes, static_cast<qint64>(data.size()));
        writeFailureBytes = 0;
        return downloadedFile.write(data.constData(), writable);
    }
#endif
    return downloadedFile.write(data);
}

bool GzipDownloader::start(QNetworkReply *reply, QDir _extractionPath)
{
    if(!reply || networkReply || extractThread)
    {
        if (reply)
            emit errorOccurred(QStringLiteral("Downloader is already active"));
        return false;
    }

    const QVariant contentLength = reply->header(QNetworkRequest::ContentLengthHeader);
    if(contentLength.isValid() && contentLength.toLongLong() > maxCompressedBytes)
    {
        reply->abort();
        reply->deleteLater();
        emit errorOccurred(QStringLiteral("Downloaded archive exceeds size limit"));
        return false;
    }

    if(!QDir().mkpath(TMP_DIR))
    {
        emit errorOccurred(QStringLiteral("Cannot create download directory"));
        return false;
    }

    downloadedFile.setFileName(TMP_DIR + QDateTime::currentDateTime().toString("yyyy_MM_dd_hhmmss_zzz") + ".tar.gz");
    if(!downloadedFile.open(QIODevice::WriteOnly | QIODevice::Unbuffered))
    {
        emit errorOccurred(QStringLiteral("Cannot create temporary download file"));
        return false;
    }

    extractionPath = _extractionPath;
    networkReply = reply;
    completionEmitted = false;
    connect(networkReply, &QIODevice::readyRead, this, &GzipDownloader::onDataAvailable);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(networkReply, &QNetworkReply::errorOccurred, this, &GzipDownloader::onErrorOccurred);
#else
    connect(networkReply, qOverload<QNetworkReply::NetworkError>(&QNetworkReply::error), this, &GzipDownloader::onErrorOccurred);
#endif
    connect(networkReply, &QNetworkReply::downloadProgress, this, &GzipDownloader::downloadProgressUpdated);
    connect(networkReply, &QNetworkReply::finished, this, &GzipDownloader::onArchiveReady);
    return true;
}

void GzipDownloader::abort()
{
    cleanup();
}

bool GzipDownloader::isActive()
{
    return networkReply;
}

QNetworkAccessManager* GzipDownloader::getManager()
{
    return nam;
}

void GzipDownloader::onDataAvailable()
{
    if(!isActive()) {
        return;
    }

    const QByteArray dat = networkReply->readAll();
    if (dat.size() > maxCompressedBytes - downloadedFile.size() ||
        writeDownloadedData(dat) != dat.size())
    {
        if (!completionEmitted)
        {
            completionEmitted = true;
            emit errorOccurred(QStringLiteral("Downloaded archive exceeds size limit or cannot be written"));
        }
        cleanup();
    }
}

void GzipDownloader::onArchiveReady()
{
    if(!isActive()) {
        return;
    }

    if(networkReply->error() != QNetworkReply::NoError)
    {
        emit errorOccurred(networkReply->errorString());
        cleanup();
    }
    else
    {
        const QByteArray dat = networkReply->readAll();
        if (dat.size() > maxCompressedBytes - downloadedFile.size() ||
            writeDownloadedData(dat) != dat.size())
        {
            if (!completionEmitted)
            {
                completionEmitted = true;
                emit errorOccurred(QStringLiteral("Downloaded archive exceeds size limit or cannot be written"));
            }
            cleanup();
            return;
        }
        downloadedFile.close();

        emit decompressionStarted();

        extractThread = new ExtractionThread(extractionPath.path(), downloadedFile.fileName(), this
#ifdef JDSP_TEST_HOOKS
                                             , extractionInterruptionCheck
#else
                                             , {}
#endif
                                             , packageValidator
        );
        connect(extractThread, &ExtractionThread::onFinished, this, &GzipDownloader::onArchiveExtracted);
        connect(extractThread, &ExtractionThread::validationStarted, this, &GzipDownloader::validationStarted);
        connect(extractThread, &QThread::finished, this, [this]() {
            if (extractThread)
            {
                extractThread->deleteLater();
                extractThread = nullptr;
            }
            downloadedFile.remove();
        });
        extractThread->start();
    }
}

void GzipDownloader::onArchiveExtracted(const QString &errorString)
{
    if (completionEmitted)
        return;
    completionEmitted = true;
    if(errorString.isEmpty())
    {
        emit success();
    }
    else
    {
        emit errorOccurred(errorString);
    }
    cleanup();
}

void GzipDownloader::onErrorOccurred(QNetworkReply::NetworkError ex)
{
    Q_UNUSED(ex)
    // Note: Already handled by finish() signal
    // emit errorOccurred(QVariant::fromValue(ex).toString());
    // cleanup();
}

void GzipDownloader::cleanup()
{
    if(networkReply)
    {
        networkReply->abort();
        networkReply->deleteLater();
        networkReply = nullptr;
    }
    if(extractThread) {
        extractThread->requestInterruption();
        // ExtractionThread may still be reading this archive. Its finished
        // handler closes/removes the file only after run() has returned.
        return;
    }

    downloadedFile.close();
    if(!downloadedFile.fileName().isEmpty())
        downloadedFile.remove();
}
