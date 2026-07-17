#include "ProjectDocument.h"
#include "ArchaeologyMetadata.h"
#include "CloudLayer.h"
#include "CrsService.h"
#include "RecentFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int MaxCloudFiles = 1'000;
constexpr qint64 MaxProjectBytes = 64 * 1024 * 1024;
constexpr int MaxWindowStateBytes = 4 * 1024 * 1024;
constexpr int MaxLayerNameLength = 512;
constexpr int MaxAttributeSummaries = 256;
constexpr int MaxArchaeologyFields = 128;
constexpr int MaxControlPoints = 10000;
constexpr int MaxMeasurements3d = 10000;
constexpr int MaxMeasurementPoints = 10000;
constexpr double MaxExactJsonInteger = 9'007'199'254'740'991.0;

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

bool validMetadataInteger(qint64 value)
{
    return value >= -1
        && static_cast<double>(value) <= MaxExactJsonInteger;
}

bool readMetadataInteger(const QJsonValue& value, qint64& output)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < -1.0
        || number > MaxExactJsonInteger || std::floor(number) != number)
    {
        return false;
    }
    output = static_cast<qint64>(number);
    return true;
}

QString colorModeName(famp::display::ColorMode mode)
{
    switch (mode)
    {
    case famp::display::ColorMode::PointRgb:
        return QStringLiteral("point-rgb");
    case famp::display::ColorMode::Uniform:
        return QStringLiteral("uniform");
    case famp::display::ColorMode::Elevation:
        return QStringLiteral("elevation");
    case famp::display::ColorMode::Attribute:
        return QStringLiteral("attribute");
    }
    return {};
}

bool colorModeFromName(const QString& name, famp::display::ColorMode& mode)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("point-rgb"))
        mode = famp::display::ColorMode::PointRgb;
    else if (normalized == QStringLiteral("uniform"))
        mode = famp::display::ColorMode::Uniform;
    else if (normalized == QStringLiteral("elevation"))
        mode = famp::display::ColorMode::Elevation;
    else if (normalized == QStringLiteral("attribute"))
        mode = famp::display::ColorMode::Attribute;
    else
        return false;
    return true;
}

QJsonObject serializeDisplay(const famp::display::Settings& display)
{
    return QJsonObject{
        {QStringLiteral("pointSize"), display.pointSize},
        {QStringLiteral("opacity"), display.opacity},
        {QStringLiteral("colorMode"), colorModeName(display.colorMode)},
        {QStringLiteral("red"), display.red},
        {QStringLiteral("green"), display.green},
        {QStringLiteral("blue"), display.blue},
        {QStringLiteral("automaticScalarRange"), display.automaticScalarRange},
        {QStringLiteral("scalarMinimum"), display.scalarMinimum},
        {QStringLiteral("scalarMaximum"), display.scalarMaximum},
        {QStringLiteral("attributeName"), display.attributeName.trimmed()}};
}

bool deserializeDisplay(const QJsonValue& value,
                        famp::display::Settings& display)
{
    if (!value.isObject())
        return false;
    const QJsonObject object = value.toObject();
    const QJsonValue pointSize = object.value(QStringLiteral("pointSize"));
    const QJsonValue opacity = object.value(QStringLiteral("opacity"));
    const QJsonValue colorMode = object.value(QStringLiteral("colorMode"));
    const QJsonValue red = object.value(QStringLiteral("red"));
    const QJsonValue green = object.value(QStringLiteral("green"));
    const QJsonValue blue = object.value(QStringLiteral("blue"));
    const QJsonValue automatic = object.value(
        QStringLiteral("automaticScalarRange"));
    const QJsonValue minimum = object.value(QStringLiteral("scalarMinimum"));
    const QJsonValue maximum = object.value(QStringLiteral("scalarMaximum"));
    const QJsonValue attributeName = object.value(QStringLiteral("attributeName"));
    if (!pointSize.isDouble() || !opacity.isDouble() || !colorMode.isString()
        || !red.isDouble() || !green.isDouble() || !blue.isDouble()
        || !automatic.isBool() || !minimum.isDouble() || !maximum.isDouble()
        || (!attributeName.isUndefined() && !attributeName.isString()))
    {
        return false;
    }

    famp::display::Settings candidate;
    candidate.pointSize = pointSize.toDouble();
    candidate.opacity = opacity.toDouble();
    candidate.red = red.toDouble();
    candidate.green = green.toDouble();
    candidate.blue = blue.toDouble();
    candidate.automaticScalarRange = automatic.toBool();
    candidate.scalarMinimum = minimum.toDouble();
    candidate.scalarMaximum = maximum.toDouble();
    candidate.attributeName = attributeName.isString()
        ? attributeName.toString().trimmed() : QString();
    if (!colorModeFromName(colorMode.toString(), candidate.colorMode)
        || !famp::display::validateSettings(candidate))
    {
        return false;
    }
    display = candidate;
    return true;
}

bool displayAttributeExists(
    const famp::display::Settings& display,
    const QVector<famp::cloud::AttributeSummary>& attributes)
{
    if (display.colorMode != famp::display::ColorMode::Attribute)
        return true;
    const QString selected = display.attributeName.trimmed().toCaseFolded();
    return std::any_of(
        attributes.cbegin(), attributes.cend(),
        [&selected](const famp::cloud::AttributeSummary& attribute) {
            return attribute.name.trimmed().toCaseFolded() == selected;
        });
}

QJsonArray serializeAttributes(
    const QVector<famp::cloud::AttributeSummary>& attributes)
{
    QJsonArray result;
    for (const famp::cloud::AttributeSummary& attribute : attributes)
    {
        result.append(QJsonObject{
            {QStringLiteral("name"), attribute.name.trimmed()},
            {QStringLiteral("unit"), attribute.unit.trimmed()},
            {QStringLiteral("type"),
             famp::cloud::attributeValueTypeName(attribute.type)},
            {QStringLiteral("valueCount"),
             static_cast<double>(attribute.valueCount)},
            {QStringLiteral("finiteValueCount"),
             static_cast<double>(attribute.finiteValueCount)},
            {QStringLiteral("hasFiniteRange"), attribute.hasFiniteRange},
            {QStringLiteral("minimum"), attribute.minimum},
            {QStringLiteral("maximum"), attribute.maximum}});
    }
    return result;
}

bool deserializeAttributes(
    const QJsonValue& value,
    QVector<famp::cloud::AttributeSummary>& attributes)
{
    if (!value.isArray() || value.toArray().size() > MaxAttributeSummaries)
        return false;
    QVector<famp::cloud::AttributeSummary> candidate;
    QSet<QString> names;
    for (const QJsonValue& item : value.toArray())
    {
        if (!item.isObject())
            return false;
        const QJsonObject object = item.toObject();
        const QJsonValue name = object.value(QStringLiteral("name"));
        const QJsonValue unit = object.value(QStringLiteral("unit"));
        const QJsonValue type = object.value(QStringLiteral("type"));
        const QJsonValue finiteRange = object.value(
            QStringLiteral("hasFiniteRange"));
        const QJsonValue minimum = object.value(QStringLiteral("minimum"));
        const QJsonValue maximum = object.value(QStringLiteral("maximum"));
        famp::cloud::AttributeSummary summary;
        if (!name.isString() || !unit.isString() || !type.isString()
            || !finiteRange.isBool() || !minimum.isDouble()
            || !maximum.isDouble()
            || !readMetadataInteger(
                object.value(QStringLiteral("valueCount")), summary.valueCount)
            || !readMetadataInteger(
                object.value(QStringLiteral("finiteValueCount")),
                summary.finiteValueCount)
            || !famp::cloud::attributeValueTypeFromName(
                type.toString(), summary.type))
        {
            return false;
        }
        summary.name = name.toString().trimmed();
        summary.unit = unit.toString().trimmed();
        summary.hasFiniteRange = finiteRange.toBool();
        summary.minimum = minimum.toDouble();
        summary.maximum = maximum.toDouble();
        const QString key = summary.name.toCaseFolded();
        if (names.contains(key)
            || !famp::cloud::validateAttributeSummary(summary))
        {
            return false;
        }
        names.insert(key);
        candidate.append(summary);
    }
    attributes = candidate;
    return true;
}

bool validArchaeologyFields(const QMap<QString, QString>& fields)
{
    return famp::archaeology::validateFields(fields);
}

QJsonObject serializeArchaeologyFields(const QMap<QString, QString>& fields)
{
    QJsonObject result;
    for (auto iterator = fields.cbegin(); iterator != fields.cend(); ++iterator)
        result.insert(iterator.key().trimmed(), iterator.value());
    return result;
}

bool deserializeArchaeologyFields(
    const QJsonValue& value,
    QMap<QString, QString>& fields)
{
    if (!value.isObject() || value.toObject().size() > MaxArchaeologyFields)
        return false;
    QMap<QString, QString> candidate;
    const QJsonObject object = value.toObject();
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator)
    {
        if (!iterator.value().isString())
            return false;
        candidate.insert(iterator.key(), iterator.value().toString());
    }
    if (!validArchaeologyFields(candidate))
        return false;
    fields = candidate;
    return true;
}

QJsonArray serializeMeasurements3d(
    const QVector<famp::measurement::Record3D>& measurements)
{
    QJsonArray result;
    for (const auto& measurement : measurements)
    {
        QJsonArray points;
        for (const QVector3D& point : measurement.points)
        {
            points.append(QJsonArray{
                static_cast<double>(point.x()),
                static_cast<double>(point.y()),
                static_cast<double>(point.z())});
        }
        result.append(QJsonObject{
            {QStringLiteral("id"), measurement.id.trimmed().toLower()},
            {QStringLiteral("layerId"),
             measurement.layerId.trimmed().toLower()},
            {QStringLiteral("crs"),
             measurement.crs.trimmed().isEmpty()
                 ? QString()
                 : famp::crs::normalizedEpsg(measurement.crs)},
            {QStringLiteral("kind"),
             famp::measurement::kindName(measurement.kind)},
            {QStringLiteral("points"), points}});
    }
    return result;
}

bool deserializeMeasurements3d(
    const QJsonValue& value,
    const QHash<QString, QString>& layerCrsById,
    QVector<famp::measurement::Record3D>& measurements)
{
    if (value.isUndefined())
    {
        measurements.clear();
        return true;
    }
    if (!value.isArray() || value.toArray().size() > MaxMeasurements3d)
        return false;

    QVector<famp::measurement::Record3D> candidate;
    QSet<QString> ids;
    for (const QJsonValue& item : value.toArray())
    {
        if (!item.isObject())
            return false;
        const QJsonObject object = item.toObject();
        const QJsonValue idValue = object.value(QStringLiteral("id"));
        const QJsonValue layerIdValue = object.value(QStringLiteral("layerId"));
        const QJsonValue crsValue = object.value(QStringLiteral("crs"));
        const QJsonValue kindValue = object.value(QStringLiteral("kind"));
        const QJsonValue pointsValue = object.value(QStringLiteral("points"));
        if (!idValue.isString() || !layerIdValue.isString()
            || !crsValue.isString() || !kindValue.isString()
            || !pointsValue.isArray()
            || pointsValue.toArray().size() > MaxMeasurementPoints)
        {
            return false;
        }

        famp::measurement::Record3D record;
        record.id = idValue.toString().trimmed().toLower();
        record.layerId = layerIdValue.toString().trimmed().toLower();
        const QString storedCrs = crsValue.toString();
        record.crs = storedCrs.trimmed().isEmpty()
            ? QString() : famp::crs::normalizedEpsg(storedCrs);
        if ((!storedCrs.trimmed().isEmpty() && record.crs.isEmpty())
            || !famp::measurement::kindFromName(
                kindValue.toString(), record.kind))
        {
            return false;
        }
        record.points.reserve(pointsValue.toArray().size());
        for (const QJsonValue& pointValue : pointsValue.toArray())
        {
            if (!pointValue.isArray() || pointValue.toArray().size() != 3)
                return false;
            const QJsonArray coordinates = pointValue.toArray();
            if (!coordinates.at(0).isDouble()
                || !coordinates.at(1).isDouble()
                || !coordinates.at(2).isDouble())
            {
                return false;
            }
            const double x = coordinates.at(0).toDouble();
            const double y = coordinates.at(1).toDouble();
            const double z = coordinates.at(2).toDouble();
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
                || std::abs(x) > std::numeric_limits<float>::max()
                || std::abs(y) > std::numeric_limits<float>::max()
                || std::abs(z) > std::numeric_limits<float>::max())
            {
                return false;
            }
            record.points.append(QVector3D(
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z)));
        }

        if (ids.contains(record.id)
            || !layerCrsById.contains(record.layerId)
            || layerCrsById.value(record.layerId) != record.crs
            || !famp::measurement::validateRecord3D(record))
        {
            return false;
        }
        ids.insert(record.id);
        candidate.append(record);
    }
    measurements = candidate;
    return true;
}

bool validSha256Hex(const QString& value)
{
    if (value.isEmpty())
        return true;
    if (value.size() != 64)
        return false;
    for (const QChar character : value)
    {
        const ushort code = character.unicode();
        const bool digit = code >= '0' && code <= '9';
        const bool lower = code >= 'a' && code <= 'f';
        const bool upper = code >= 'A' && code <= 'F';
        if (!digit && !lower && !upper)
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

QJsonArray serializeControlPoints(
    const QVector<famp::control::Point>& points)
{
    QJsonArray result;
    for (const famp::control::Point& point : points)
    {
        result.append(QJsonObject{
            {QStringLiteral("id"), point.id.trimmed().toLower()},
            {QStringLiteral("name"), point.name.trimmed()},
            {QStringLiteral("enabled"), point.enabled},
            {QStringLiteral("local"), numberArray(point.local)},
            {QStringLiteral("target"), numberArray(point.target)}});
    }
    return result;
}

bool deserializeControlPoints(
    const QJsonValue& value,
    QVector<famp::control::Point>& points)
{
    if (value.isUndefined())
    {
        points.clear();
        return true;
    }
    if (!value.isArray() || value.toArray().size() > MaxControlPoints)
        return false;

    QVector<famp::control::Point> candidate;
    candidate.reserve(value.toArray().size());
    for (const QJsonValue& item : value.toArray())
    {
        if (!item.isObject())
            return false;
        const QJsonObject object = item.toObject();
        const QJsonValue idValue = object.value(QStringLiteral("id"));
        const QJsonValue nameValue = object.value(QStringLiteral("name"));
        const QJsonValue enabledValue = object.value(QStringLiteral("enabled"));
        famp::control::Point point;
        if (!idValue.isString() || !nameValue.isString()
            || !enabledValue.isBool()
            || !readNumberArray(
                object.value(QStringLiteral("local")), point.local)
            || !readNumberArray(
                object.value(QStringLiteral("target")), point.target))
        {
            return false;
        }
        point.id = idValue.toString().trimmed().toLower();
        point.name = nameValue.toString().trimmed();
        point.enabled = enabledValue.toBool();
        candidate.append(point);
    }
    if (!famp::control::validatePoints(candidate))
        return false;
    points = candidate;
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
    QSet<QString> uniqueLayerIds;
    QHash<QString, QString> layerCrsById;
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
            || !validMetadataInteger(reference.size)
            || !validMetadataInteger(reference.modifiedUtcMilliseconds)
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

        const QFileInfo info(normalizedCloudPath);
        const QString layerId = reference.layerId.trimmed().isEmpty()
            ? famp::cloud::stableLayerId(normalizedCloudPath)
            : reference.layerId.trimmed().toLower();
        const QString layerName = reference.name.trimmed().isEmpty()
            ? info.fileName() : reference.name.trimmed();
        const QString layerCrs = reference.crs.trimmed().isEmpty()
            ? projectCrs : famp::crs::normalizedEpsg(reference.crs);
        if (!famp::cloud::isValidLayerId(layerId)
            || uniqueLayerIds.contains(layerId))
        {
            setError(errorMessage,
                     QStringLiteral("点云图层 ID 无效或重复：%1")
                         .arg(reference.layerId));
            return false;
        }
        if (layerName.isEmpty() || layerName.size() > MaxLayerNameLength)
        {
            setError(errorMessage, QStringLiteral("点云图层名称无效：%1")
                                       .arg(reference.name));
            return false;
        }
        if (!reference.crs.trimmed().isEmpty() && layerCrs.isEmpty())
        {
            setError(errorMessage, QStringLiteral("点云图层坐标系无效：%1")
                                       .arg(reference.crs));
            return false;
        }
        if (!famp::display::validateSettings(reference.display)
            || reference.attributes.size() > MaxAttributeSummaries
            || !validArchaeologyFields(reference.archaeologyFields)
            || !famp::control::validatePoints(reference.controlPoints))
        {
            setError(errorMessage,
                     QStringLiteral("点云图层显示、属性、考古字段或控制点无效：%1")
                         .arg(layerName));
            return false;
        }
        QSet<QString> attributeNames;
        for (const famp::cloud::AttributeSummary& attribute : reference.attributes)
        {
            const QString key = attribute.name.trimmed().toCaseFolded();
            if (attributeNames.contains(key)
                || !validMetadataInteger(attribute.valueCount)
                || !validMetadataInteger(attribute.finiteValueCount)
                || !famp::cloud::validateAttributeSummary(attribute))
            {
                setError(errorMessage,
                         QStringLiteral("点云图层属性摘要无效：%1")
                             .arg(layerName));
                return false;
            }
            attributeNames.insert(key);
        }
        if (!displayAttributeExists(reference.display, reference.attributes))
        {
            setError(errorMessage,
                     QStringLiteral("点云图层的着色属性不存在：%1")
                         .arg(layerName));
            return false;
        }

        uniquePaths.append(normalizedCloudPath);
        uniqueLayerIds.insert(layerId);
        layerCrsById.insert(layerId, layerCrs);
        const QString relativePath = QDir::fromNativeSeparators(
            projectDirectory.relativeFilePath(normalizedCloudPath));
        cloudFiles.append(relativePath);

        const qint64 size = reference.size >= 0
            ? reference.size : (info.exists() ? info.size() : -1);
        const qint64 modified = reference.modifiedUtcMilliseconds >= 0
            ? reference.modifiedUtcMilliseconds
            : (info.exists() ? info.lastModified().toUTC().toMSecsSinceEpoch() : -1);
        if (!validMetadataInteger(size) || !validMetadataInteger(modified))
        {
            setError(errorMessage,
                     QStringLiteral("点云文件元数据超出可安全保存的范围：%1")
                         .arg(reference.path));
            return false;
        }
        QJsonObject serializedReference;
        serializedReference.insert(QStringLiteral("relativePath"), relativePath);
        serializedReference.insert(QStringLiteral("absolutePath"),
                                   QDir::fromNativeSeparators(normalizedCloudPath));
        serializedReference.insert(QStringLiteral("layerId"), layerId);
        serializedReference.insert(QStringLiteral("name"), layerName);
        serializedReference.insert(QStringLiteral("crs"), layerCrs);
        serializedReference.insert(QStringLiteral("size"),
                                   static_cast<double>(size));
        serializedReference.insert(QStringLiteral("modifiedUtcMilliseconds"),
                                   static_cast<double>(modified));
        serializedReference.insert(QStringLiteral("sha256"),
                                   QString::fromLatin1(reference.sha256.toHex()));
        serializedReference.insert(QStringLiteral("visible"), reference.visible);
        serializedReference.insert(QStringLiteral("locked"), reference.locked);
        serializedReference.insert(QStringLiteral("origin"),
                                   numberArray(reference.spatial.origin));
        serializedReference.insert(QStringLiteral("transform"),
                                   numberArray(reference.spatial.transform));
        serializedReference.insert(QStringLiteral("display"),
                                   serializeDisplay(reference.display));
        serializedReference.insert(QStringLiteral("attributes"),
                                   serializeAttributes(reference.attributes));
        serializedReference.insert(
            QStringLiteral("archaeologyFields"),
            serializeArchaeologyFields(reference.archaeologyFields));
        serializedReference.insert(
            QStringLiteral("controlPoints"),
            serializeControlPoints(reference.controlPoints));
        cloudReferences.append(serializedReference);
    }

    if (document.measurements3d.size() > MaxMeasurements3d)
    {
        setError(errorMessage, QStringLiteral("三维测量成果数量超过安全上限。"));
        return false;
    }
    QSet<QString> measurementIds;
    for (const auto& measurement : document.measurements3d)
    {
        const QString id = measurement.id.trimmed().toLower();
        const QString layerId = measurement.layerId.trimmed().toLower();
        const QString measurementCrs = measurement.crs.trimmed().isEmpty()
            ? QString() : famp::crs::normalizedEpsg(measurement.crs);
        if (measurementIds.contains(id)
            || !uniqueLayerIds.contains(layerId)
            || layerCrsById.value(layerId) != measurementCrs
            || !famp::measurement::validateRecord3D(measurement))
        {
            setError(errorMessage, QStringLiteral("项目包含无效或重复的三维测量成果。"));
            return false;
        }
        measurementIds.insert(id);
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
    root.insert(QStringLiteral("measurements3d"),
                serializeMeasurements3d(document.measurements3d));
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
    if (schemaVersion < 1 || schemaVersion > SchemaVersion)
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
        QSet<QString> referencedLayerIds;
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
            reference.layerId = famp::cloud::stableLayerId(resolvedPath);
            reference.name = QFileInfo(resolvedPath).fileName();
            reference.crs = projectCrs;
            const QString hash = hashValue.toString();
            reference.visible = visibleValue.toBool();
            if (!readMetadataInteger(sizeValue, reference.size)
                || !readMetadataInteger(
                    modifiedValue, reference.modifiedUtcMilliseconds)
                || !validSha256Hex(hash)
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
            reference.sha256 = QByteArray::fromHex(hash.toLatin1());
            if (schemaVersion >= 3)
            {
                const QJsonValue layerIdValue = object.value(
                    QStringLiteral("layerId"));
                const QJsonValue nameValue = object.value(QStringLiteral("name"));
                const QJsonValue crsValue = object.value(QStringLiteral("crs"));
                const QJsonValue lockedValue = object.value(QStringLiteral("locked"));
                if (!layerIdValue.isString() || !nameValue.isString()
                    || !crsValue.isString() || !lockedValue.isBool())
                {
                    setError(errorMessage,
                             QStringLiteral("项目包含无效的点云图层字段。"));
                    return false;
                }
                reference.layerId = layerIdValue.toString().trimmed().toLower();
                reference.name = nameValue.toString().trimmed();
                const QString storedLayerCrs = crsValue.toString();
                reference.crs = storedLayerCrs.trimmed().isEmpty()
                    ? QString()
                    : famp::crs::normalizedEpsg(storedLayerCrs);
                reference.locked = lockedValue.toBool();
                if (!famp::cloud::isValidLayerId(reference.layerId)
                    || reference.name.isEmpty()
                    || reference.name.size() > MaxLayerNameLength
                    || (!storedLayerCrs.trimmed().isEmpty()
                        && reference.crs.isEmpty())
                    || !deserializeDisplay(
                        object.value(QStringLiteral("display")),
                        reference.display)
                    || !deserializeAttributes(
                        object.value(QStringLiteral("attributes")),
                        reference.attributes)
                    || !deserializeArchaeologyFields(
                        object.value(QStringLiteral("archaeologyFields")),
                        reference.archaeologyFields)
                    || !deserializeControlPoints(
                        object.value(QStringLiteral("controlPoints")),
                        reference.controlPoints))
                {
                    setError(errorMessage,
                             QStringLiteral("项目包含无效的点云图层状态。"));
                    return false;
                }
                if (!displayAttributeExists(
                        reference.display, reference.attributes))
                {
                    setError(errorMessage,
                             QStringLiteral("项目引用了不存在的点云着色属性。"));
                    return false;
                }
            }
            if (!containsPath(referencedPaths, reference.path))
            {
                if (referencedLayerIds.contains(reference.layerId))
                {
                    setError(errorMessage, QStringLiteral("项目包含重复的点云图层 ID。"));
                    return false;
                }
                referencedLayerIds.insert(reference.layerId);
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
            reference.layerId = famp::cloud::stableLayerId(path);
            reference.name = QFileInfo(path).fileName();
            reference.crs = projectCrs;
            cloudReferences.append(reference);
        }
    }

    QHash<QString, QString> loadedLayerCrsById;
    for (const CloudReference& reference : cloudReferences)
        loadedLayerCrsById.insert(reference.layerId, reference.crs);
    QVector<famp::measurement::Record3D> measurements3d;
    if (schemaVersion >= 3
        && !deserializeMeasurements3d(
            root.value(QStringLiteral("measurements3d")),
            loadedLayerCrsById,
            measurements3d))
    {
        setError(errorMessage, QStringLiteral("项目中的三维测量成果无效。"));
        return false;
    }

    Document loaded;
    loaded.cloudFiles = cloudFiles;
    loaded.clouds = cloudReferences;
    loaded.mapScale = mapScale;
    loaded.projectCrs = projectCrs;
    loaded.graphicsState = graphicsState;
    loaded.measurements3d = measurements3d;
    loaded.windowGeometry = windowGeometry;
    loaded.windowState = windowState;
    loaded.xoyLabelVisible = xoyLabelVisible;
    loaded.scaleVisible = scaleVisible;
    document = loaded;
    return true;
}
}
