#include "ProjectDocument.h"
#include "CrsService.h"
#include "RecentFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace
{
constexpr int MaxCloudFiles = 1'000;
constexpr qint64 MaxProjectBytes = 8 * 1024 * 1024;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

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

bool isSupportedScale(const QString& scale)
{
    static const QStringList supportedScales{
        QStringLiteral("1:10"),
        QStringLiteral("1:20"),
        QStringLiteral("1:50"),
        QStringLiteral("1:100")};
    return supportedScales.contains(scale);
}
}

namespace famp::project
{
QString pathWithProjectSuffix(const QString& path)
{
    QString result = path.trimmed();
    if (result.isEmpty() || result.endsWith(QStringLiteral(".famp"), Qt::CaseInsensitive))
        return result;
    while (result.endsWith(QLatin1Char('.')))
        result.chop(1);
    return result + QStringLiteral(".famp");
}

bool save(const QString& projectPath,
          const Document& document,
          const QString& applicationVersion,
          QString* errorMessage)
{
    const QString normalizedProjectPath = pathWithProjectSuffix(projectPath);
    if (normalizedProjectPath.isEmpty())
    {
        setError(errorMessage, QStringLiteral("项目路径不能为空。"));
        return false;
    }

    if (!isSupportedScale(document.mapScale))
    {
        setError(errorMessage,
                 QStringLiteral("无效的项目比例尺：%1").arg(document.mapScale));
        return false;
    }
    if (document.cloudFiles.size() > MaxCloudFiles)
    {
        setError(errorMessage, QStringLiteral("项目中的点云文件数量超过上限。"));
        return false;
    }
    const QString projectCrs = document.projectCrs.trimmed().isEmpty()
        ? QString()
        : famp::crs::normalizedEpsg(document.projectCrs);
    if (!document.projectCrs.trimmed().isEmpty() && projectCrs.isEmpty())
    {
        setError(errorMessage,
                 QStringLiteral("无效的项目坐标系：%1").arg(document.projectCrs));
        return false;
    }

    const QDir projectDirectory(QFileInfo(normalizedProjectPath).absolutePath());
    QJsonArray cloudFiles;
    QStringList uniquePaths;
    for (const QString& cloudPath : document.cloudFiles)
    {
        const QString normalizedCloudPath = famp::recent::normalizedPath(cloudPath);
        if (normalizedCloudPath.isEmpty()
            || !famp::recent::isSupportedCloudFile(normalizedCloudPath))
        {
            setError(errorMessage,
                     QStringLiteral("项目包含无效的点云路径：%1").arg(cloudPath));
            return false;
        }
        if (containsPath(uniquePaths, normalizedCloudPath))
            continue;

        uniquePaths.append(normalizedCloudPath);
        cloudFiles.append(QDir::fromNativeSeparators(
            projectDirectory.relativeFilePath(normalizedCloudPath)));
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("FAMP Project"));
    root.insert(QStringLiteral("schemaVersion"), SchemaVersion);
    root.insert(QStringLiteral("applicationVersion"), applicationVersion);
    root.insert(QStringLiteral("savedAtUtc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("mapScale"), document.mapScale);
    root.insert(QStringLiteral("projectCrs"), projectCrs);
    root.insert(QStringLiteral("cloudFiles"), cloudFiles);

    QSaveFile file(normalizedProjectPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法创建项目文件 %1：%2")
                     .arg(normalizedProjectPath, file.errorString()));
        return false;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit())
    {
        setError(errorMessage,
                 QStringLiteral("无法完成项目文件 %1：%2")
                     .arg(normalizedProjectPath, file.errorString()));
        return false;
    }
    return true;
}

bool load(const QString& projectPath,
          Document& document,
          QString* errorMessage)
{
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法打开项目文件 %1：%2")
                     .arg(projectPath, file.errorString()));
        return false;
    }
    if (file.size() > MaxProjectBytes)
    {
        setError(errorMessage, QStringLiteral("项目文件超过 8 MiB 安全上限。"));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        setError(errorMessage,
                 QStringLiteral("项目 JSON 无效：%1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("FAMP Project"))
    {
        setError(errorMessage, QStringLiteral("该文件不是 FAMP 项目。"));
        return false;
    }
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != SchemaVersion)
    {
        setError(errorMessage,
                 QStringLiteral("不支持的 FAMP 项目格式版本。"));
        return false;
    }

    const QString mapScale = root.value(QStringLiteral("mapScale")).toString();
    if (!isSupportedScale(mapScale))
    {
        setError(errorMessage, QStringLiteral("项目中的比例尺无效。"));
        return false;
    }
    const QString storedCrs = root.value(QStringLiteral("projectCrs")).toString();
    const QString projectCrs = storedCrs.trimmed().isEmpty()
        ? QString()
        : famp::crs::normalizedEpsg(storedCrs);
    if (!storedCrs.trimmed().isEmpty() && projectCrs.isEmpty())
    {
        setError(errorMessage, QStringLiteral("项目中的坐标系无效。"));
        return false;
    }

    const QJsonValue cloudValue = root.value(QStringLiteral("cloudFiles"));
    if (!cloudValue.isArray() || cloudValue.toArray().size() > MaxCloudFiles)
    {
        setError(errorMessage, QStringLiteral("项目中的点云列表无效。"));
        return false;
    }

    const QDir projectDirectory(QFileInfo(projectPath).absolutePath());
    QStringList cloudFiles;
    for (const QJsonValue& value : cloudValue.toArray())
    {
        if (!value.isString() || value.toString().trimmed().isEmpty())
        {
            setError(errorMessage, QStringLiteral("项目包含无效的点云路径。"));
            return false;
        }

        const QString storedPath = QDir::fromNativeSeparators(value.toString());
        const QString resolvedPath = famp::recent::normalizedPath(
            QFileInfo(storedPath).isAbsolute()
                ? storedPath
                : projectDirectory.absoluteFilePath(storedPath));
        if (!famp::recent::isSupportedCloudFile(resolvedPath))
        {
            setError(errorMessage,
                     QStringLiteral("项目包含不支持的点云文件：%1").arg(storedPath));
            return false;
        }
        if (!containsPath(cloudFiles, resolvedPath))
            cloudFiles.append(resolvedPath);
    }

    Document loaded;
    loaded.cloudFiles = cloudFiles;
    loaded.mapScale = mapScale;
    loaded.projectCrs = projectCrs;
    document = loaded;
    return true;
}
}
