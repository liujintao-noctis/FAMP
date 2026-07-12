#pragma once

#include <QString>
#include <QStringList>

namespace famp::recent
{
inline constexpr int MaxFiles = 8;

bool isSupportedCloudFile(const QString& path);
QString normalizedPath(const QString& path);
QStringList updatedFiles(const QStringList& currentFiles,
                         const QString& openedPath,
                         int maximumCount = MaxFiles);
QStringList availableFiles(const QStringList& storedFiles,
                           int maximumCount = MaxFiles);
}
