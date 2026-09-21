#include <cassert>
#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "interface/LiveprogSelectionWidget.h"
#include "interface/QAnimatedSlider.h"
#include "eeleditor.h"
#include "model/codecontainer.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());

    const QString scriptPath = temporary.filePath("saved-editor-script.eel");
    QFile script(scriptPath);
    assert(script.open(QIODevice::WriteOnly | QIODevice::Text));
    assert(script.write("@init\nfoo = 1;\n@sample\nspl0 = spl0;\n") > 0);
    script.close();

    LiveprogSelectionWidget widget;
    QSignalSpy reloads(&widget, &LiveprogSelectionWidget::liveprogReloadRequested);

    widget.updateFromEelEditor(scriptPath);
    assert(widget.currentLiveprog() == scriptPath);
    assert(reloads.count() == 1);

    const QString secondPath = temporary.filePath("saved-editor-script-2.eel");
    QFile second(secondPath);
    assert(second.open(QIODevice::WriteOnly | QIODevice::Text));
    assert(second.write("@init\nbar = 2;\n@sample\nspl1 = spl1;\n") > 0);
    second.close();

    widget.updateFromEelEditor(secondPath);
    assert(widget.currentLiveprog() == secondPath);
    assert(reloads.count() == 2);

    /* Exercise the same signal route used by MainWindow, rather than calling
     * the selection slot directly. One editor execution request must produce
     * one reload request and adopt the requested path. */
    EELEditor editor;
    QObject::connect(&editor, &EELEditor::executionRequested,
                     &widget, [&widget](const QString& path) {
                         widget.updateFromEelEditor(path);
                     });
    emit editor.executionRequested(scriptPath);
    assert(widget.currentLiveprog() == scriptPath);
    assert(reloads.count() == 3);

    const QString precisePath = temporary.filePath("precise-editor-script.eel");
    QFile precise(precisePath);
    assert(precise.open(QIODevice::WriteOnly | QIODevice::Text));
    assert(precise.write("@init\nprecision:0.1<0.1,1,0.001>Precision\nprecision = 0.1;\nmode:0<0,1,1{A,B}>Mode\nmode = 0;\n@sample\nspl0 = spl0;\n") > 0);
    precise.close();
    widget.setCurrentLiveprog(precisePath);
    auto *precisionSlider = widget.findChild<QAnimatedSlider *>("precision");
    assert(precisionSlider != nullptr);
    assert(precisionSlider->minimum() == 100);
    assert(precisionSlider->maximum() == 1000);
    assert(precisionSlider->singleStep() == 1);

    QSignalSpy changes(&widget, &LiveprogSelectionWidget::liveprogVariableChanged);
    precisionSlider->setValueA(500, false);
    assert(QMetaObject::invokeMethod(precisionSlider, "sliderReleased", Qt::DirectConnection));
    assert(changes.count() == 1);
    const QList<QVariant> change = changes.at(0);
    assert(change.at(0).toString() == "precision");
    assert(std::abs(change.at(1).toFloat() - 0.5f) < 0.0001f);
    QFile updated(precisePath);
    assert(updated.open(QIODevice::ReadOnly));
    const QByteArray updatedSource = updated.readAll();
    assert(updatedSource.contains("precision = 0.500;"));

    auto *modeBox = widget.findChild<QComboBox *>("mode");
    assert(modeBox != nullptr);
    modeBox->setCurrentIndex(1);
    QFile listUpdated(precisePath);
    assert(listUpdated.open(QIODevice::ReadOnly));
    assert(listUpdated.readAll().contains("mode = 1;"));

    assert(QMetaObject::invokeMethod(&widget, "onResetLiveprogParams", Qt::DirectConnection));
    QFile reset(precisePath);
    assert(reset.open(QIODevice::ReadOnly));
    const QByteArray resetSource = reset.readAll();
    assert(resetSource.contains("precision = 0.100;"));
    assert(resetSource.contains("mode = 0;"));
    assert(reloads.count() == 4);

    precisionSlider = widget.findChild<QAnimatedSlider *>("precision");
    assert(precisionSlider != nullptr);
    QSignalSpy saveErrors(&widget, &LiveprogSelectionWidget::unitLabelUpdateRequested);
    const int reloadsBeforeFailedEdit = reloads.count();
    const int changesBeforeFailedEdit = changes.count();
    precisionSlider->setValueA(500, false);
    CodeContainerSetSaveCommitFailureForTests(true);
    assert(QMetaObject::invokeMethod(precisionSlider, "sliderReleased", Qt::DirectConnection));
    CodeContainerSetSaveCommitFailureForTests(false);
    assert(saveErrors.count() == 1);
    assert(saveErrors.constFirst().at(0).toString().contains(precisePath));
    assert(reloads.count() == reloadsBeforeFailedEdit);
    assert(changes.count() == changesBeforeFailedEdit);
    QFile unchangedAfterFailedEdit(precisePath);
    assert(unchangedAfterFailedEdit.open(QIODevice::ReadOnly));
    assert(unchangedAfterFailedEdit.readAll().contains("precision = 0.100;"));

    widget.setActive(false);
    assert(!widget.isActive());
    widget.setActive(true);
    assert(widget.isActive());
    return 0;
}
