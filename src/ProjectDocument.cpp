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

#include <cmath>

namespace
{
constexpr int MaxCloudFiles = 1'000;
constexpr qint64 MaxProjectBytes = 64 * 1024 * 1024;
constexpr int MaxWindowStateBytes = 4 * 1024 * 1024;

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

bool isPlausibleGraphicsState(const QJsonObject& state)
{
    if (state.isEmpty())
        return true;
    return state.value(QStringLiteral("schemaVersion")).isDouble()
        && state.value(QStringLiteral("sceneRect")).isArray()
        && state.value(QStringLiteral("items")).isArray();
}

bool validSpatialReference(const famp::cloud::SpatialReference& spatial)
{
    for (double coordinate : spatial.origin)
    {
        if (!std::isfinite(coordinate))
            return false;
    }
    for (double component : spatial.transform)
    {
        if (!std::isfinite(component))
            return false;
    }
    return true;
}

QJsonArray numberArray(const std::array<double, 3>& values)
{
    return QJsonArray{values[0], values[1], values[2]};
}

QJsonArray numberArray(const std::array<double, 16>& values)
{
    QJsonArray result;
    for (double value : values)
        result.append(value);
    return result;
}

template <std::size_t Size>
bool readNumberArray(const QJsonValue& value, std::array<double, Size>& output)
{
    if (!value.isArray())
        return false;
    const QJsonArray values = value.toArray();
    if (values.size() != static_cast<int>(Size))
        return false;
    std::array<double, Size> candidate{};
    for (int index = 0; index < values.size(); ++index)
    {
        if (!values.at(index).isDouble())
            return false;
        candidate[static_cast<std::size_t>(index)] = values.at(index).toDouble();
        if (!std::isfinite(candidate[static_cast<std::size_t>(index)]))
            return false;
    }
    output = candidate;
    return true;
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
    const int cloudCount = document.clouds.isEmpty()
        ? document.cloudFiles.size() : document.clouds.size();
    if (cloudCount > MaxCloudFiles)
    {
        setError(errorMessage, QStringLiteral("项目中的点云文件数量超过上限。"));
        return false;
    }
    if (!isPlausibleGraphicsState(document.graphicsState))
    {
        setError(errorMessage, QStringLiteral("二维画布状态无效。"));
        return false;
    }
    if (document.windowGeometry.size() > MaxWindowStateBytes
        || document.windowState.size() > MaxWindowStateBytes)
    {
        setError(errorMessage, QStringLiteral("窗口布局状态超过安全上限。"));
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
    QJsonArray cloudReferences;
    QStringList uniquePaths;
    QVector<CloudReference> references = document.clouds;
    if (references.isEmpty())
    {
        references.reserve(document.cloudFiles.size());
        for (const QString& path : document.cloudFiles)
        {
            CloudReference reference;
            reference.path = path;
            references.append(reference);
        }
    }
    for (const CloudReference& reference : references)
    {
        const QString normalizedCloudPath = famp::recent::normalizedPath(
            reference.path);
        if (normalizedCloudPath.isEmpty()
            || !famp::recent::isSupportedCloudFile(normalizedCloudPath))
        {
            setError(errorMessage,
                     QStringLiteral("项目包含无效的点云路径：%1")
                         .arg(reference.path));
            return false;
        }
        if (!validSpatialReference(reference.spatial)
            || (reference.size < -1)
            || (reference.modifiedUtcMilliseconds < -1)
            || (!reference.sha256.isEmpty()
                && reference.sha256.size() != 32))
        {
            setError(errorMessage,
                     QStringLiteral("点云空间或文件元数据无效：%1")
                         .arg(reference.path));
            return false;
        }
        if (containsPath(uniquePaths, normalizedCloudPath))
            continue;

        uniquePaths.append(normalizedCloudPath);
        const QString relativePath = QDir::fromNativeSeparators(
            projectDirectory.relativeFilePath(normalizedCloudPath));
        cloudFiles.append(relativePath);

        const QFileInfo info(normalizedCloudPath);
        const qint64 size = reference.size >= 0
            ? reference.size : (info.exists() ? info.size() : -1);
        const qint64 modified = reference.modifiedUtcMilliseconds >= 0
            ? reference.modifiedUtcMilliseconds
            : (info.exists() ? info.lastModified().toUTC().toMSecsSinceEpoch() : -1);
        QJsonObject serializedReference;
        serializedReference.insert(QStringLiteral("relativePath"), relativePath);
        serializedReference.insert(QStringLiteral("absolutePath"),
                                   QDir::fromNativeSeparators(normalizedCloudPath));
        serializedReference.insert(QStringLiteral("size"),
                                   static_cast<double>(size));
        serializedReference.insert(QStringLiteral("modifiedUtcMilliseconds"),
                                   static_cast<double>(modified));
        serializedReference.insert(QStringLiteral("sha256"),
                                   QString::fromLatin1(reference.sha256.toHex()));
        serializedReference.insert(QStringLiteral("visible"), reference.visible);
        serializedReference.insert(QStringLiteral("origin"),
                                   numberArray(reference.spatial.origin));
        serializedReference.insert(QStringLiteral("transform"),
                                   numberArray(reference.spatial.transform));
        cloudReferences.append(serializedReference);
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
    root.insert(QStringLiteral("clouds"), cloudReferences);
    root.insert(QStringLiteral("graphicsState"), document.graphicsState);
    root.insert(QStringLiteral("windowGeometry"),
                QString::fromLatin1(document.windowGeometry.toBase64()));
    root.insert(QStringLiteral("windowState"),
                QString::fromLatin1(document.windowState.toBase64()));
    root.insert(QStringLiteral("xoyLabelVisible"), document.xoyLabelVisible);
    root.insert(QStringLiteral("scaleVisible"), document.scaleVisible);

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (json.size() > MaxProjectBytes)
    {
        setError(errorMessage,
                 QStringLiteral("项目内容超过 64 MiB 安全上限，请简化二维图元。"));
        return false;
    }
    QSaveFile file(normalizedProjectPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法创建项目文件 %1：%2")
                     .arg(normalizedProjectPath, file.errorString()));
        return false;
    }
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
        setError(errorMessage, QStringLiteral("项目文件超过 64 MiB 安全上限。"));
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
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 1 && schemaVersion != SchemaVersion)
    {
        setError(errorMessage,
                 QStringLiteral("不支持的 FAMP 项目格式版本。"));
        return false;
    }

    QJsonObject graphicsState;
    QByteArray windowGeometry;
    QByteArray windowState;
    bool xoyLabelVisible = true;
    bool scaleVisible = true;
    if (schemaVersion >= 2)
    {
        const QJsonValue graphicsValue = root.value(QStringLiteral("graphicsState"));
        const QJsonValue geometryValue = root.value(QStringLiteral("windowGeometry"));
        const QJsonValue stateValue = root.value(QStringLiteral("windowState"));
        const QJsonValue xoyValue = root.value(QStringLiteral("xoyLabelVisible"));
        const QJsonValue scaleVisibleValue = root.value(QStringLiteral("scaleVisible"));
        if (!graphicsValue.isObject() || !geometryValue.isString()
            || !stateValue.isString() || !xoyValue.isBool()
            || !scaleVisibleValue.isBool())
        {
            setError(errorMessage, QStringLiteral("项目窗口或二维画布状态无效。"));
            return false;
        }
        graphicsState = graphicsValue.toObject();
        if (!isPlausibleGraphicsState(graphicsState))
        {
            setError(errorMessage, QStringLiteral("项目二维画布状态无效。"));
            return false;
        }
        const QByteArray encodedGeometry = geometryValue.toString().toLatin1();
        const QByteArray encodedState = stateValue.toString().toLatin1();
        windowGeometry = QByteArray::fromBase64(encodedGeometry);
        windowState = QByteArray::fromBase64(encodedState);
        if ((!encodedGeometry.isEmpty() && windowGeometry.isEmpty())
            || (!encodedState.isEmpty() && windowState.isEmpty())
            || windowGeometry.size() > MaxWindowStateBytes
            || windowState.size() > MaxWindowStateBytes)
        {
            setError(errorMessage, QStringLiteral("项目窗口布局编码无效。"));
            return false;
        }
        xoyLabelVisible = xoyValue.toBool();
        scaleVisible = scaleVisibleValue.toBool();
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

    QVector<CloudReference> cloudReferences;
    if (schemaVersion >= 2)
    {
        const QJsonValue referencesValue = root.value(QStringLiteral("clouds"));
        if (!referencesValue.isArray()
            || referencesValue.toArray().size() > MaxCloudFiles)
        {
            setError(errorMessage, QStringLiteral("项目中的点云元数据列表无效。"));
            return false;
        }
        QStringList referencedPaths;
        for (const QJsonValue& value : referencesValue.toArray())
        {
            if (!value.isObject())
            {
                setError(errorMessage, QStringLiteral("项目包含无效的点云元数据。"));
                return false;
            }
            const QJsonObject object = value.toObject();
            const QJsonValue relativeValue = object.value(QStringLiteral("relativePath"));
            const QJsonValue absoluteValue = object.value(QStringLiteral("absolutePath"));
            const QJsonValue sizeValue = object.value(QStringLiteral("size"));
            const QJsonValue modifiedValue = object.value(
                QStringLiteral("modifiedUtcMilliseconds"));
            const QJsonValue hashValue = object.value(QStringLiteral("sha256"));
            const QJsonValue visibleValue = object.value(QStringLiteral("visible"));
            if (!relativeValue.isString() || !absoluteValue.isString()
                || !sizeValue.isDouble() || !modifiedValue.isDouble()
                || !hashValue.isString() || !visibleValue.isBool())
            {
                setError(errorMessage, QStringLiteral("项目包含无效的点云元数据字段。"));
                return false;
            }
            const QString relativePath = QDir::fromNativeSeparators(
                relativeValue.toString());
            const QString absolutePath = QDir::fromNativeSeparators(
                absoluteValue.toString());
            const QString relativeCandidate = famp::recent::normalizedPath(
                projectDirectory.absoluteFilePath(relativePath));
            const QString absoluteCandidate = famp::recent::normalizedPath(
                absolutePath);
            QString resolvedPath = QFileInfo::exists(relativeCandidate)
                ? relativeCandidate
                : (QFileInfo::exists(absoluteCandidate)
                       ? absoluteCandidate : relativeCandidate);
            if (!famp::recent::isSupportedCloudFile(resolvedPath))
            {
                setError(errorMessage,
                         QStringLiteral("项目包含不支持的点云文件：%1")
                             .arg(relativePath));
                return false;
            }

            CloudReference reference;
            reference.path = resolvedPath;
            reference.size = static_cast<qint64>(sizeValue.toDouble(-1.0));
            reference.modifiedUtcMilliseconds = static_cast<qint64>(
                modifiedValue.toDouble(-1.0));
            reference.sha256 = QByteArray::fromHex(
                hashValue.toString().toLatin1());
            reference.visible = visibleValue.toBool();
            if (reference.size < -1 || reference.modifiedUtcMilliseconds < -1
                || (!hashValue.toString().isEmpty()
                    && (hashValue.toString().size() != 64
                        || reference.sha256.size() != 32))
                || !readNumberArray(
                    object.value(QStringLiteral("origin")),
                    reference.spatial.origin)
                || !readNumberArray(
                    object.value(QStringLiteral("transform")),
                    reference.spatial.transform))
            {
                setError(errorMessage,
                         QStringLiteral("项目包含无效的点云空间或文件元数据。"));
                return false;
            }
            if (!containsPath(referencedPaths, reference.path))
            {
                referencedPaths.append(reference.path);
                cloudReferences.append(reference);
            }
        }
        cloudFiles = referencedPaths;
    }
    if (cloudReferences.isEmpty() && !cloudFiles.isEmpty())
    {
        cloudReferences.reserve(cloudFiles.size());
        for (const QString& path : cloudFiles)
        {
            CloudReference reference;
            reference.path = path;
            cloudReferences.append(reference);
        }
    }

    Document loaded;
    loaded.cloudFiles = cloudFiles;
    loaded.clouds = cloudReferences;
    loaded.mapScale = mapScale;
    loaded.projectCrs = projectCrs;
    loaded.graphicsState = graphicsState;
    loaded.windowGeometry = windowGeometry;
    loaded.windowState = windowState;
    loaded.xoyLabelVisible = xoyLabelVisible;
    loaded.scaleVisible = scaleVisible;
    document = loaded;
    return true;
}
}
