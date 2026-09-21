#ifndef EXTRACTIONTHREAD_H
#define EXTRACTIONTHREAD_H

#include <QThread>
#include <QFile>
#include <QDir>
#include <QString>
#include <functional>
#include <utility>

#include "Untar.h"

class ExtractionThread : public QThread
{
    Q_OBJECT

public:
    using PackageValidator = std::function<QString(const QString&, const std::function<bool()>&)>;

    ExtractionThread(QString _extractionPath,
                     QString _downloadedFileName,
                     QObject *parent = nullptr,
                     std::function<bool()> _interruptionCheck = {},
                     PackageValidator _packageValidator = {}) : QThread(parent) {
        extractionPath = _extractionPath;
        downloadedFileName = _downloadedFileName;
        interruptionCheck = std::move(_interruptionCheck);
        packageValidator = std::move(_packageValidator);
    };
    ~ExtractionThread() {};

signals:
    void onFinished(const QString& error);

protected:
    void run() override {
        QDir destination = QDir(extractionPath);
        QFile file = QFile(downloadedFileName);

        QString errorMsg;
        destination.mkpath(destination.path());
        const auto interrupted = [this]() {
            return interruptionCheck ? interruptionCheck() : isInterruptionRequested();
        };
        if (interrupted()) {
            emit onFinished(QStringLiteral("Extraction cancelled"));
            return;
        }
        int ret = Untar::extract(file.fileName(), destination.path(), errorMsg,
                                 interrupted);

        if (interrupted()) {
            emit onFinished(QStringLiteral("Extraction cancelled"));
            return;
        }

        if(ret > 0) {
            emit onFinished(errorMsg);
        }
        else {
            if (packageValidator) {
                emit validationStarted();
                errorMsg = packageValidator(destination.path(), interrupted);
                if (interrupted()) {
                    emit onFinished(QStringLiteral("Package validation cancelled"));
                    return;
                }
                if (!errorMsg.isEmpty()) {
                    emit onFinished(errorMsg);
                    return;
                }
            }
            emit onFinished(QString());
        }
    }

signals:
    void validationStarted();

private:
    QString extractionPath;
    QString downloadedFileName;
    std::function<bool()> interruptionCheck;
    PackageValidator packageValidator;
};

#endif // EXTRACTIONTHREAD_H
