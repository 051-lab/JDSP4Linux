#include "AeqPackageManager.h"
#include "AeqPackageValidation.h"
#include "GzipDownloaderDialog.h"
#include "HttpException.h"

#include "config/AppConfig.h"
#include "data/SafeFileOperations.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTemporaryDir>
#include <utility>

#define REPO_ROOT QString("https://raw.githubusercontent.com/ThePBone/AutoEqPackages/main")

AeqPackageManager::AeqPackageManager(QObject *parent, QString databaseOverride) :
    QObject(parent), nam(new QNetworkAccessManager(this)), databaseOverride(std::move(databaseOverride))
{
    nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QtPromise::QPromise<void> AeqPackageManager::installPackage(AeqVersion version, QWidget* hostWindow)
{
    return QtPromise::QPromise<void>{[this, version, hostWindow](
        const QtPromise::QPromiseResolve<void>& resolve,
                const QtPromise::QPromiseReject<void>& reject) {

            auto reply = nam->get(QNetworkRequest(QUrl(version.packageUrl)));
            const QString database = databaseDirectory();
            if(!QDir().mkpath(QFileInfo(database).absolutePath()))
            {
                reply->deleteLater();
                reject();
                return;
            }
            QTemporaryDir staging(QFileInfo(database).absolutePath() + "/.autoeq-staging-XXXXXX");
            if(!staging.isValid())
            {
                reply->deleteLater();
                reject();
                return;
            }

            QPointer<GzipDownloaderDialog> downloader = new GzipDownloaderDialog(reply, QDir(staging.path()), hostWindow,
                [](const QString& path, const std::function<bool()>& cancellationRequested) {
                    if (AeqPackageValidation::validPackage(path, cancellationRequested))
                        return QString();
                    return cancellationRequested() ? QStringLiteral("Package validation cancelled")
                                                   : QStringLiteral("Downloaded package is invalid");
                });
            bool success = downloader->exec();
            if(downloader)
                downloader->deleteLater();

            if(success && AeqPackageValidation::publishPackage(staging.path(), database))
                resolve();
            else
                reject();
        }};
}

bool AeqPackageManager::uninstallPackage()
{
    return QDir(databaseDirectory()).removeRecursively();
}

bool AeqPackageManager::isPackageInstalled()
{
    return AeqPackageValidation::validPackage(databaseDirectory());
}

QtPromise::QPromise<AeqVersion> AeqPackageManager::isUpdateAvailable()
{
    return QtPromise::QPromise<AeqVersion>{[this](
        const QtPromise::QPromiseResolve<AeqVersion>& resolve,
        const QtPromise::QPromiseReject<AeqVersion>& reject) {

            QtPromisePrivate::qtpromise_defer([=, this]() {

                this->getRepositoryVersion().then([=, this](AeqVersion remote){
                    this->getLocalVersion().then([=](AeqVersion local){
                        if(remote.packageTime > local.packageTime)
                        {
                            // Remote is newer
                            resolve(remote);
                        }
                        else
                        {
                            reject(remote);
                        }
                    }).fail([=]{
                        // Local file not available, choose remote
                        resolve(remote);
                    });
                }).fail([=](const HttpException& error) {
                    // API error
                    reject(error);
                });
            });
        }};
}

QtPromise::QPromise<AeqVersion> AeqPackageManager::getRepositoryVersion()
{
    return QtPromise::QPromise<AeqVersion>{[&](
        const QtPromise::QPromiseResolve<AeqVersion>& resolve,
                const QtPromise::QPromiseReject<AeqVersion>& reject) {

            auto reqProto = QNetworkRequest(QUrl(REPO_ROOT + "/version.json"));
            reqProto.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::RedirectPolicy::NoLessSafeRedirectPolicy);

            QtPromise::connect(nam, &QNetworkAccessManager::finished).then([=](QNetworkReply *reply)
            {
                if(reply->error() != QNetworkReply::NoError)
                {
                    throw HttpException(1, reply->errorString());
                }

                QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                QVariant reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);

                if (statusCode.toInt() == 200)
                {
                    QJsonDocument d = QJsonDocument::fromJson(reply->readAll());
                    QJsonArray root = d.array();
                    for(const auto& item : root)
                    {
                        QJsonObject pkg = item.toObject();
                        QJsonArray types = pkg.value("type").toArray();

                        // Select correct package
                        if(types.contains(QJsonValue("GraphicEQ")) &&
                                types.contains(QJsonValue("CSV")) &&
                                types.count() == 2)
                        {
                            resolve(AeqVersion(pkg));
                        }
                    }

                    throw HttpException(900, "Requested package type currently unavailable");
                }
                else
                {
                    throw HttpException(statusCode.toInt(), reason.toString());
                }
            }).fail([reject](const HttpException& error) {
                reject(error);
            });

            nam->get(reqProto);
        }
    };
}

QtPromise::QPromise<AeqVersion> AeqPackageManager::getLocalVersion()
{
    return QtPromise::QPromise<AeqVersion>{[&](
        const QtPromise::QPromiseResolve<AeqVersion>& resolve,
                const QtPromise::QPromiseReject<AeqVersion>& reject) {
            QFile versionJson(databaseDirectory() + "/version.json");
            if(!versionJson.exists())
            {
                reject();
                return;
            }

            if(!versionJson.open(QFile::ReadOnly))
            {
                reject();
                return;
            }
            QJsonParseError error{};
            QJsonDocument d = QJsonDocument::fromJson(versionJson.readAll(), &error);
            QJsonArray root = d.array();
            if(error.error == QJsonParseError::NoError && d.isArray() && root.count() > 0)
            {
                versionJson.close();
                resolve(AeqVersion(root[0].toObject()));
                return;
            }

            versionJson.close();
            reject();
        }
    };
}

QtPromise::QPromise<QVector<AeqMeasurement>> AeqPackageManager::getLocalIndex()
{
    return QtPromise::QPromise<QVector<AeqMeasurement>>{[&](
        const QtPromise::QPromiseResolve<QVector<AeqMeasurement>>& resolve,
                const QtPromise::QPromiseReject<QVector<AeqMeasurement>>& reject) {
            QFile indexJson(databaseDirectory() + "/index.json");
            if(!indexJson.exists())
            {
                reject();
                return;
            }

            if(!indexJson.open(QFile::ReadOnly))
            {
                reject();
                return;
            }
            QJsonParseError error{};
            QJsonDocument d = QJsonDocument::fromJson(indexJson.readAll(), &error);
            if(error.error != QJsonParseError::NoError || !d.isArray())
            {
                indexJson.close();
                reject();
                return;
            }
            QJsonArray root = d.array();

            QVector<AeqMeasurement> items;
            for(const auto& item : root)
            {
                items.append(AeqMeasurement(item.toObject()));
            }

            indexJson.close();
            resolve(std::move(items));
        }
    };
}

QString AeqPackageManager::databaseDirectory()
{
    if(!databaseOverride.isEmpty())
        return databaseOverride;
    return AppConfig::instance().getCachePath("autoeq");
}
