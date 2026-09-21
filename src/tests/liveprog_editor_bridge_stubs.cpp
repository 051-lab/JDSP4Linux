#include "eeleditor.h"

EELEditor::EELEditor(QWidget* parent) : QMainWindow(parent)
{
}

EELEditor::~EELEditor() = default;

void EELEditor::openNewScript(QString)
{
}

void EELEditor::newProject()
{
}

void EELEditor::openProject() {}
void EELEditor::saveProject() {}
void EELEditor::saveProjectAs() {}
void EELEditor::runCode() {}
void EELEditor::goToLine() {}
void EELEditor::jumpToFunction() {}
void EELEditor::onCompilerStarted(const QString&) {}
void EELEditor::onCompilerFinished(int, const QString&, const QString&, const QString&, float) {}
void EELEditor::onConsoleOutputReceived(const QString&) {}
void EELEditor::onIsCodeLoadedChanged(bool) {}
void EELEditor::onCurrentFileUpdated(CodeContainer*, CodeContainer*) {}
void EELEditor::onBackendRefreshRequired() {}
void EELEditor::closeEvent(QCloseEvent*) {}
