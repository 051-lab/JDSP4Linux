#include "GzipDownloaderDialog.h"
#include "ui_FileDownloaderDialog.h"

GzipDownloaderDialog::GzipDownloaderDialog(QNetworkReply* _reply, QDir _targetDirectory, QWidget *parent,
                                           GzipDownloader::PackageValidator packageValidator) :
    QDialog(parent),
    ui(new Ui::FileDownloaderDialog)
{
    ui->setupUi(this);

    gzip = new GzipDownloader(this);
    gzip->setPackageValidator(std::move(packageValidator));

    ui->size->setText("");
    ui->progress->setValue(0);

    connect(gzip, &GzipDownloader::success, this, &GzipDownloaderDialog::onSuccess);

    connect(gzip, &GzipDownloader::errorOccurred, this, &GzipDownloaderDialog::onError);
    connect(gzip, &GzipDownloader::downloadProgressUpdated, this, &GzipDownloaderDialog::onDownloadProgressUpdated);
    connect(gzip, &GzipDownloader::decompressionStarted, this, &GzipDownloaderDialog::onDecompressionStarted);
    connect(gzip, &GzipDownloader::validationStarted, this, &GzipDownloaderDialog::onValidationStarted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &GzipDownloaderDialog::reject);

    reply = _reply;
    targetDirectory = _targetDirectory;
}

GzipDownloaderDialog::~GzipDownloaderDialog()
{
    delete ui;
}

void GzipDownloaderDialog::reject()
{
    if(!closeAllowed)
    {
        gzip->abort();
        return;
    }

    gzip->abort();
    QDialog::reject();
}

void GzipDownloaderDialog::showEvent(QShowEvent *ev)
{
    QDialog::showEvent(ev);
    gzip->start(reply, targetDirectory);
}

void GzipDownloaderDialog::closeEvent(QCloseEvent *ev)
{
    if(!closeAllowed)
    {
        gzip->abort();
        ev->ignore();
        return;
    }

    gzip->abort();
    disconnect(gzip, &GzipDownloader::errorOccurred, this, &GzipDownloaderDialog::onError);
    disconnect(gzip, &GzipDownloader::downloadProgressUpdated, this, &GzipDownloaderDialog::onDownloadProgressUpdated);
    disconnect(gzip, &GzipDownloader::decompressionStarted, this, &GzipDownloaderDialog::onDecompressionStarted);
    gzip->deleteLater();

    QDialog::closeEvent(ev);
}

void GzipDownloaderDialog::onSuccess()
{
    closeAllowed = true;
    accept();
}

void GzipDownloaderDialog::onError(const QString& msg)
{
    closeAllowed = true;
    if(!msg.contains(QStringLiteral("cancel"), Qt::CaseInsensitive))
        QMessageBox::critical(this, tr("Error"), msg);
    this->reject();
}

void GzipDownloaderDialog::onDownloadProgressUpdated(qint64 recv, qint64 total)
{

    ui->progress->setValue((double)recv/total * 100);
    ui->size->setText(tr("%1MB of %2MB downloaded")
                      .arg(QString::number(recv/1000000.f, 'g', 2))
                      .arg(QString::number(total/1000000.f, 'g', 2)));
}

void GzipDownloaderDialog::onDecompressionStarted()
{
    closeAllowed = false;

    ui->progress->setMinimum(0);
    ui->progress->setMaximum(0);
    ui->title->setText(tr("Decompressing package..."));
    ui->size->setText("");
    ui->buttonBox->setEnabled(false);
}

void GzipDownloaderDialog::onValidationStarted()
{
    ui->title->setText(tr("Validating package..."));
    ui->buttonBox->setEnabled(true);
}
