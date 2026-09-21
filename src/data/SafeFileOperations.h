#ifndef SAFEFILEOPERATIONS_H
#define SAFEFILEOPERATIONS_H

#include <QString>
#include <QDir>

namespace SafeFileOperations
{
bool copyAtomically(const QString& source, const QString& destination, QString* error = nullptr);
bool isSafeName(const QString& name);
bool renameWithinDirectory(const QDir& directory, const QString& sourceName,
                           const QString& destinationName, QString* error = nullptr);
bool removeWithinDirectory(const QDir& directory, const QString& name, QString* error = nullptr);

#ifdef JDSP_TEST_HOOKS
void setCopyFailureForTests(bool fail);
void setRenameFailureForTests(bool fail);
void setRemoveFailureForTests(bool fail);
#endif
}

#endif
