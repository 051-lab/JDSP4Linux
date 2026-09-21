#ifndef GZIPDOWNLOADER_H
#define GZIPDOWNLOADER_H

#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#include <utility>

#include <QtPromise>

#define TMP_DIR "/tmp/jamesdsp/download/"

class ExtractionThread;

class GzipDownloader : public QObject
{
    Q_OBJECT
public:
    using PackageValidator = std::function<QString(const QString&, const std::function<bool()>&)>;

    explicit GzipDownloader(QObject* parent = nullptr) : QObject(parent), nam(new QNetworkAccessManager(this)){}

    ~GzipDownloader();

#ifdef JDSP_TEST_HOOKS
    void setWriteFailureForTests(qint64 bytesBeforeShortWrite);
    void setExtractionInterruptionCheckForTests(std::function<bool()> check);
#endif

    bool start(QNetworkReply* reply, QDir _extractionPath);
    void setPackageValidator(PackageValidator validator) { packageValidator = std::move(validator); }
    void abort();
    bool isActive();
    QNetworkAccessManager* getManager();

signals:
    void downloadProgressUpdated(qint64 bytesReceived, qint64 bytesTotal);
    void decompressionStarted();
    void validationStarted();
    void success();
    void errorOccurred(QString errorString);

private slots:
    void onDataAvailable();
    void onArchiveReady();
    void onArchiveExtracted(const QString& errorString);
    void onErrorOccurred(QNetworkReply::NetworkError error);

    void cleanup();

private:
    static constexpr qint64 maxCompressedBytes = 128LL * 1024LL * 1024LL;

    QDir extractionPath;
    QFile downloadedFile;
    QNetworkAccessManager* nam;
    QPointer<QNetworkReply> networkReply = nullptr;
    ExtractionThread *extractThread = nullptr;
    bool completionEmitted = false;
    PackageValidator packageValidator;
#ifdef JDSP_TEST_HOOKS
    std::function<bool()> extractionInterruptionCheck;
#endif
    qint64 writeDownloadedData(const QByteArray& data);
};


#endif // GZIPDOWNLOADER_H
