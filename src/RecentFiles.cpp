#include "RecentFiles.h"

#include <QDir>
#include <QFileInfo>

namespace
{
Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool containsPath(const QStringList& paths, const QString& candidate)
{
    for (const QString& path : paths)
    {
        if (path.compare(candidate, pathCaseSensitivity()) == 0)
            return true;
    }
    return false;
}
}

namespace famp::recent
{
bool isSupportedCloudFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(QStringLiteral("pcd"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("las"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("ply"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("xyz"), Qt::CaseInsensitive) == 0;
}

QString normalizedPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};

    const QFileInfo fileInfo(path);
    const QString canonicalPath = fileInfo.canonicalFilePath();
    if (!canonicalPath.isEmpty())
        return QDir::cleanPath(canonicalPath);
    return QDir::cleanPath(fileInfo.absoluteFilePath());
}

QStringList updatedFiles(const QStringList& currentFiles,
                         const QString& openedPath,
                         int maximumCount)
{
    QStringList result;
    if (maximumCount <= 0)
        return result;

    if (isSupportedCloudFile(openedPath))
    {
        const QString normalizedOpenedPath = normalizedPath(openedPath);
        if (!normalizedOpenedPath.isEmpty())
            result.append(normalizedOpenedPath);
    }
    for (const QString& path : currentFiles)
    {
        if (result.size() >= maximumCount)
            break;

        const QString normalized = normalizedPath(path);
        if (normalized.isEmpty()
            || !isSupportedCloudFile(normalized)
            || containsPath(result, normalized))
        {
            continue;
        }

        result.append(normalized);
    }
    return result;
}

QStringList availableFiles(const QStringList& storedFiles, int maximumCount)
{
    QStringList result;
    if (maximumCount <= 0)
        return result;

    for (const QString& path : storedFiles)
    {
        const QString normalized = normalizedPath(path);
        const QFileInfo fileInfo(normalized);
        if (!fileInfo.exists()
            || !fileInfo.isFile()
            || !isSupportedCloudFile(normalized)
            || containsPath(result, normalized))
        {
            continue;
        }

        result.append(normalized);
        if (result.size() >= maximumCount)
            break;
    }
    return result;
}
}
