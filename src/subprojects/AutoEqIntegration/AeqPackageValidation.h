#ifndef AEQPACKAGEVALIDATION_H
#define AEQPACKAGEVALIDATION_H

#include <functional>

#include <QString>

namespace AeqPackageValidation
{
using CancellationCheck = std::function<bool()>;
bool validPackage(const QString& path, const CancellationCheck& cancellationCheck = {});
using RenameOperation = std::function<bool(const QString& source, const QString& destination)>;
bool publishPackage(const QString& staging, const QString& destination,
                    const RenameOperation& renameOperation = RenameOperation());
}

#endif
