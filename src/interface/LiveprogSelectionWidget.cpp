#include "LiveprogSelectionWidget.h"
#include "ui_LiveprogSelectionWidget.h"

#include "config/AppConfig.h"
#include "data/EelParser.h"
#include "interface/event/ScrollFilter.h"
#include "QAnimatedSlider.h"

#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <algorithm>
#include <cmath>
#include <eeleditor.h>

static int decimalPlaces(float value)
{
    for (int places = 0; places <= 6; ++places)
    {
        const double scale = std::pow(10.0, places);
        if (std::abs(static_cast<double>(value) * scale -
                     std::round(static_cast<double>(value) * scale)) < 0.000001)
            return places;
    }
    return 6;
}

static int sliderScale(const EELNumberRangeProperty<float> *property)
{
    const int places = std::max({decimalPlaces(property->getMinimum()),
                                 decimalPlaces(property->getMaximum()),
                                 decimalPlaces(property->getStep())});
    return static_cast<int>(std::pow(10.0, std::min(places, 6)));
}

LiveprogSelectionWidget::LiveprogSelectionWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LiveprogSelectionWidget)
{
    ui->setupUi(this);

    _eelParser = new EELParser();

    ui->files->setFileTypes(QStringList("*.eel"));

    ui->reset->hide();

    connect(ui->files,      &FileSelectionWidget::fileChanged, this, &LiveprogSelectionWidget::onFileChanged);
    connect(ui->reset,      &QAbstractButton::clicked, this, &LiveprogSelectionWidget::onResetLiveprogParams);
    connect(ui->enable,     &QCheckBox::stateChanged, this, &LiveprogSelectionWidget::toggled);
    connect(ui->editScript, &QAbstractButton::clicked, this, &LiveprogSelectionWidget::onEditScript);

    if (!QFile(_currentLiveprog).exists())
    {
        ui->files->setCurrentDirectory(AppConfig::instance().getLiveprogPath());
    }
    else
    {
        ui->files->setCurrentFile(_currentLiveprog);
        loadProperties(_currentLiveprog);
    }
}

LiveprogSelectionWidget::~LiveprogSelectionWidget()
{
    _eelParser->deleteLater();
    delete ui;
}

bool LiveprogSelectionWidget::isActive() const
{
    return ui->enable->isChecked();
}

void LiveprogSelectionWidget::setActive(bool active)
{
    ui->enable->setChecked(active);
}

void LiveprogSelectionWidget::coupleIDE(EELEditor* editor)
{
    _eelEditor = editor;
    ui->editScript->setVisible(editor != nullptr);
}

void LiveprogSelectionWidget::onFileChanged(const QString& path)
{
    if (!QFileInfo::exists(path) || !QFileInfo(path).isFile())
        return;

    _currentLiveprog = path;
    loadProperties(_currentLiveprog);

    emit scriptChanged(path);
}

void LiveprogSelectionWidget::onResetLiveprogParams()
{
    if (!_eelParser->loadDefaults())
    {
        QMessageBox::warning(this, tr("Error"), tr("Cannot restore defaults.\nNo EEL file is currently loaded."));
    }

    emit liveprogReloadRequested();

    loadProperties(_eelParser->getPath());
}

const QString &LiveprogSelectionWidget::currentLiveprog() const
{
    return _currentLiveprog;
}

void LiveprogSelectionWidget::setCurrentLiveprog(const QString &path)
{
    _currentLiveprog = path;
    ui->files->setCurrentFile(_currentLiveprog);
    loadProperties(_currentLiveprog);
}

void LiveprogSelectionWidget::updateFromEelEditor(QString path)
{
    this->setCurrentLiveprog(path);
    emit liveprogReloadRequested();
}

void LiveprogSelectionWidget::updateList()
{
    ui->files->enumerateFiles();
}

void LiveprogSelectionWidget::onEditScript()
{
    if(_eelEditor == nullptr)
    {
        return;
    }

    if (_currentLiveprog.isEmpty())
    {
        _eelEditor->show();
        _eelEditor->raise();
        _eelEditor->newProject();
        return;
    }
    else if (!QFile(_currentLiveprog).exists())
    {
        QMessageBox::warning(this, tr("Error"), tr("Selected EEL file does not exist anymore.\n"
                             "Please select another one"));
        return;
    }

    _eelEditor->show();
    _eelEditor->raise();
    _eelEditor->openNewScript(_currentLiveprog);
}

void LiveprogSelectionWidget::loadProperties(const QString& path)
{
    QLayoutItem *item;

    while ((item = ui->ui_container->layout()->takeAt(0)) != NULL )
    {
        delete item->widget();
        delete item;
    }

    if(path.isEmpty())
    {
        ui->reset->setVisible(false);
        ui->editScript->setText(tr("Create new script"));
        ui->name->setText(tr("No script has been loaded"));
        return;
    }

    if (!_eelParser->loadFile(path))
    {
        ui->reset->setVisible(false);
        ui->editScript->setText(tr("Create new script"));
        ui->name->setText(tr("Unable to load script"));
        QLabel *lbl = new QLabel(tr("The selected EEL file could not be read."), this);
        ui->ui_container->layout()->addWidget(lbl);
        return;
    }
    ui->name->setText(_eelParser->getDescription());
    ui->editScript->setText(tr("Edit script"));

    EELProperties props = _eelParser->getProperties();

    for (EELBaseProperty* propbase : props)
    {
        if (propbase->getType() == EELPropertyType::NumberRange)
        {
            EELNumberRangeProperty<float> *prop        = dynamic_cast<EELNumberRangeProperty<float>*>(propbase);
            bool                           handleAsInt = std::floor(prop->getStep()) == prop->getStep();
            const int                      scale       = sliderScale(prop);
            QLabel                        *lbl         = new QLabel(this);
            QAnimatedSlider               *sld         = new QAnimatedSlider(this);
            lbl->setText(prop->getDescription());

            sld->setMinimum(qRound(prop->getMinimum() * scale));
            sld->setMaximum(qRound(prop->getMaximum() * scale));
            sld->setSingleStep(1);
            sld->setValue(qRound(prop->getMinimum() * scale));
            sld->setValueA(qRound(prop->getValue() * scale));
            sld->setOrientation(Qt::Horizontal);
            sld->setToolTip(QString::number(prop->getValue()));
            sld->setProperty("isCustomEELProperty", true);
            sld->setProperty("handleAsInt", handleAsInt);
            sld->setProperty("divisor", scale);

            sld->setObjectName(prop->getKey());
            sld->installEventFilter(new ScrollFilter(sld));

            ui->ui_container->layout()->addWidget(lbl);
            ui->ui_container->layout()->addWidget(sld);

            connect(sld, &QAnimatedSlider::stringChanged, this, &LiveprogSelectionWidget::unitLabelUpdateRequested);
            connect(sld, &QAbstractSlider::sliderReleased, [this, sld, prop] {
                const int divisor = sld->property("divisor").toInt();
                float val = sld->valueA() / static_cast<float>(divisor > 0 ? divisor : 1);
                prop->setValue(val);
                if (!_eelParser->manipulateProperty(prop))
                {
                    const QString error = _eelParser->getLastSaveError();
                    emit unitLabelUpdateRequested(error.isEmpty()
                        ? tr("Unable to save the EEL parameter change.") : error);
                    return;
                }
                ui->reset->setEnabled(_eelParser->canLoadDefaults());

                emit liveprogVariableChanged(prop->getKey(), val);
            });
        }
        else if (propbase->getType() == EELPropertyType::List)
        {
            EELListProperty *prop = dynamic_cast<EELListProperty*>(propbase);
            QLabel          *lbl         = new QLabel(this);
            QComboBox       *cbx         = new QComboBox(this);
            lbl->setText(prop->getDescription());

            cbx->addItems(prop->getOptions());
            cbx->setCurrentIndex(prop->getValue());
            cbx->setProperty("isCustomEELProperty", true);

            cbx->setObjectName(prop->getKey());
            cbx->installEventFilter(new ScrollFilter(cbx));

            ui->ui_container->layout()->addWidget(lbl);
            ui->ui_container->layout()->addWidget(cbx);

            connect(cbx, qOverload<int>(&QComboBox::currentIndexChanged), [this, prop](int index) {
                if(index < prop->getMinimum() || index > prop->getMaximum())
                    return;

                prop->setValue(index);
                if (!_eelParser->manipulateProperty(prop))
                {
                    const QString error = _eelParser->getLastSaveError();
                    emit unitLabelUpdateRequested(error.isEmpty()
                        ? tr("Unable to save the EEL parameter change.") : error);
                    return;
                }
                ui->reset->setEnabled(_eelParser->canLoadDefaults());

                emit liveprogVariableChanged(prop->getKey(), (float)index);
            });
        }
    }

    if (props.isEmpty())
    {
        QLabel *lbl = new QLabel(this);
        lbl->setText(tr("No customizable parameters"));
        ui->ui_container->layout()->addWidget(lbl);
        ui->reset->hide();
    }
    else
    {
        ui->reset->setVisible(_eelParser->hasDefaultsDefined());
        ui->reset->setEnabled(_eelParser->canLoadDefaults());
    }
}
