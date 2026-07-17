/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Description: PCD loading helpers
 *****************************************************************/

#include "PcdLoader.h"
#include "QstringAndStringConvert.h"

#include <pcl/io/pcd_io.h>
#include <pcl/common/point_tests.h>

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMap>
#include <QSet>
#include <QTemporaryFile>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace
{
constexpr std::uint8_t kDefaultRed = 255;
constexpr std::uint8_t kDefaultGreen = 255;
constexpr std::uint8_t kDefaultBlue = 255;
constexpr qint64 kCopyBufferSize = 1024 * 1024;
constexpr qint64 kMaxHeaderBytes = 1024 * 1024;

enum class SpatialMetadataStatus
{
    NotPresent,
    Valid,
    Invalid
};

enum class AttributeMetadataStatus
{
    NotPresent,
    Valid,
    Invalid
};

struct PcdAttributeDescriptor
{
    int index = -1;
    QString fieldName;
    QString name;
    QString unit;
    famp::cloud::AttributeValueType type =
        famp::cloud::AttributeValueType::Float64;
};

struct ParsedPcdAttributes
{
    famp::cloud::CloudAttributes attributes;
    qint64 pointCount = 0;
};

QStringList splitPcdLine(const QString& line)
{
    return line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

void setFirstError(QString& error, const QString& message)
{
    if (error.isEmpty())
        error = message;
}

bool decodeAttributeMetadataToken(const QString& token, QString& value)
{
    if (!token.startsWith(QLatin1Char('b')))
        return false;
    const QByteArray encoded = token.mid(1).toLatin1();
    for (const char character : encoded)
    {
        const bool valid = (character >= 'A' && character <= 'Z')
            || (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9')
            || character == '-' || character == '_';
        if (!valid)
            return false;
    }
    if (encoded.size() % 4 == 1)
        return false;

    const QByteArray decoded = QByteArray::fromBase64(
        encoded, QByteArray::Base64UrlEncoding);
    const QByteArray canonical = decoded.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (canonical != encoded)
        return false;

    const QString decodedText = QString::fromUtf8(decoded);
    if (decodedText.toUtf8() != decoded)
        return false;
    value = decodedText;
    return true;
}

bool parseFloatingAttribute(const QString& token, double& value)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("nan")
        || normalized == QStringLiteral("+nan")
        || normalized == QStringLiteral("-nan"))
    {
        value = std::numeric_limits<double>::quiet_NaN();
        if (normalized.startsWith(QLatin1Char('-')))
            value = std::copysign(value, -1.0);
        return true;
    }
    if (normalized == QStringLiteral("inf")
        || normalized == QStringLiteral("+inf")
        || normalized == QStringLiteral("infinity")
        || normalized == QStringLiteral("+infinity"))
    {
        value = std::numeric_limits<double>::infinity();
        return true;
    }
    if (normalized == QStringLiteral("-inf")
        || normalized == QStringLiteral("-infinity"))
    {
        value = -std::numeric_limits<double>::infinity();
        return true;
    }

    bool ok = false;
    value = QLocale::c().toDouble(token, &ok);
    return ok;
}

AttributeMetadataStatus readEmbeddedAttributes(
    const QString& path,
    ParsedPcdAttributes& parsed,
    QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return AttributeMetadataStatus::NotPresent;

    bool valid = true;
    bool markerPresent = false;
    bool markerSeen = false;
    bool fieldsSeen = false;
    bool sizesSeen = false;
    bool typesSeen = false;
    bool countsSeen = false;
    bool pointsSeen = false;
    bool dataSeen = false;
    int declaredAttributeCount = -1;
    qint64 pointCount = -1;
    QStringList fieldNames;
    QStringList fieldSizes;
    QStringList fieldTypes;
    QStringList fieldCounts;
    QMap<int, PcdAttributeDescriptor> descriptors;
    qint64 headerBytes = 0;

    while (!file.atEnd())
    {
        const QByteArray rawLine = file.readLine(64 * 1024);
        headerBytes += rawLine.size();
        if (headerBytes > kMaxHeaderBytes
            || (!rawLine.endsWith('\n') && !file.atEnd()))
        {
            valid = false;
            setFirstError(error, QStringLiteral("PCD 头部过大或包含超长行。"));
            break;
        }

        const QString line = QString::fromLatin1(rawLine).trimmed();
        const QStringList tokens = splitPcdLine(line);
        if (tokens.isEmpty())
            continue;

        if (tokens.size() >= 2
            && tokens.at(0) == QStringLiteral("#")
            && tokens.at(1) == QStringLiteral("FAMP_ATTRIBUTES"))
        {
            markerPresent = true;
            if (markerSeen || tokens.size() != 4
                || tokens.at(2) != QStringLiteral("1"))
            {
                valid = false;
                setFirstError(error, QStringLiteral("逐点属性版本标记无效或重复。"));
            }
            markerSeen = true;
            bool countOk = false;
            const int count = tokens.value(3).toInt(&countOk);
            if (!countOk || count < 1 || count > 256)
            {
                valid = false;
                setFirstError(error, QStringLiteral("逐点属性通道数无效。"));
            }
            else
            {
                declaredAttributeCount = count;
            }
            continue;
        }

        if (tokens.size() >= 2
            && tokens.at(0) == QStringLiteral("#")
            && tokens.at(1) == QStringLiteral("FAMP_ATTRIBUTE"))
        {
            if (tokens.size() != 7)
            {
                valid = false;
                setFirstError(error, QStringLiteral("逐点属性描述行格式无效。"));
                continue;
            }
            bool indexOk = false;
            const int index = tokens.at(2).toInt(&indexOk);
            PcdAttributeDescriptor descriptor;
            descriptor.index = index;
            descriptor.fieldName = tokens.at(3);
            if (!indexOk || index < 0 || index >= 256
                || descriptors.contains(index)
                || !decodeAttributeMetadataToken(tokens.at(4), descriptor.name)
                || !decodeAttributeMetadataToken(tokens.at(5), descriptor.unit)
                || !famp::cloud::attributeValueTypeFromName(
                    tokens.at(6), descriptor.type))
            {
                valid = false;
                setFirstError(error, QStringLiteral("逐点属性描述内容无效或重复。"));
                continue;
            }
            descriptors.insert(index, descriptor);
            continue;
        }

        const QString key = tokens.at(0).toUpper();
        auto readHeaderList = [&](bool& seen, QStringList& destination) {
            if (seen || tokens.size() < 2)
            {
                valid = false;
                setFirstError(error,
                              QStringLiteral("PCD 属性字段头部缺失或重复。"));
                return;
            }
            seen = true;
            destination = tokens.mid(1);
        };
        if (key == QStringLiteral("FIELDS"))
            readHeaderList(fieldsSeen, fieldNames);
        else if (key == QStringLiteral("SIZE"))
            readHeaderList(sizesSeen, fieldSizes);
        else if (key == QStringLiteral("TYPE"))
            readHeaderList(typesSeen, fieldTypes);
        else if (key == QStringLiteral("COUNT"))
            readHeaderList(countsSeen, fieldCounts);
        else if (key == QStringLiteral("POINTS"))
        {
            if (pointsSeen || tokens.size() != 2)
            {
                valid = false;
                setFirstError(error, QStringLiteral("PCD POINTS 头部无效或重复。"));
            }
            pointsSeen = true;
            bool pointsOk = false;
            const qlonglong value = tokens.value(1).toLongLong(&pointsOk);
            if (!pointsOk || value < 1
                || value > std::numeric_limits<int>::max())
            {
                valid = false;
                setFirstError(error, QStringLiteral("PCD 属性点数无效或过大。"));
            }
            else
            {
                pointCount = value;
            }
        }
        else if (key == QStringLiteral("DATA"))
        {
            if (dataSeen || tokens.size() != 2
                || tokens.at(1).compare(
                    QStringLiteral("ascii"), Qt::CaseInsensitive) != 0)
            {
                valid = false;
                setFirstError(error,
                              QStringLiteral("含逐点属性的 FAMP PCD 必须使用 ASCII DATA。"));
            }
            dataSeen = true;
            break;
        }
    }

    if (!markerPresent)
        return AttributeMetadataStatus::NotPresent;
    if (!valid || !markerSeen || declaredAttributeCount < 1
        || !fieldsSeen || !sizesSeen || !typesSeen || !countsSeen
        || !pointsSeen || !dataSeen || pointCount < 1)
    {
        setFirstError(error, QStringLiteral("逐点属性元数据不完整。"));
        return AttributeMetadataStatus::Invalid;
    }
    if (descriptors.size() != declaredAttributeCount)
    {
        setFirstError(error, QStringLiteral("逐点属性描述数量与声明不一致。"));
        return AttributeMetadataStatus::Invalid;
    }

    const int expectedFieldCount = 4 + declaredAttributeCount;
    if (fieldNames.size() != expectedFieldCount
        || fieldSizes.size() != expectedFieldCount
        || fieldTypes.size() != expectedFieldCount
        || fieldCounts.size() != expectedFieldCount
        || fieldNames.mid(0, 4)
            != QStringList({QStringLiteral("x"), QStringLiteral("y"),
                            QStringLiteral("z"), QStringLiteral("rgb")})
        || fieldSizes.mid(0, 4)
            != QStringList({QStringLiteral("4"), QStringLiteral("4"),
                            QStringLiteral("4"), QStringLiteral("4")})
        || fieldTypes.mid(0, 4)
            != QStringList({QStringLiteral("F"), QStringLiteral("F"),
                            QStringLiteral("F"), QStringLiteral("U")})
        || fieldCounts.mid(0, 4)
            != QStringList({QStringLiteral("1"), QStringLiteral("1"),
                            QStringLiteral("1"), QStringLiteral("1")}))
    {
        setFirstError(error, QStringLiteral("PCD 基础字段或属性字段数量无效。"));
        return AttributeMetadataStatus::Invalid;
    }

    QVector<famp::cloud::AttributeChannel> channels;
    channels.reserve(declaredAttributeCount);
    QSet<QString> normalizedNames;
    const int perChannelReserve = static_cast<int>(std::min<qint64>(
        pointCount, std::max(1, 1024 * 1024 / declaredAttributeCount)));
    for (int index = 0; index < declaredAttributeCount; ++index)
    {
        if (!descriptors.contains(index))
        {
            setFirstError(error, QStringLiteral("逐点属性索引不连续。"));
            return AttributeMetadataStatus::Invalid;
        }
        const PcdAttributeDescriptor& descriptor = descriptors[index];
        const int fieldIndex = 4 + index;
        QString expectedType;
        switch (descriptor.type)
        {
        case famp::cloud::AttributeValueType::Float64:
            expectedType = QStringLiteral("F");
            break;
        case famp::cloud::AttributeValueType::SignedInteger:
            expectedType = QStringLiteral("I");
            break;
        case famp::cloud::AttributeValueType::UnsignedInteger:
            expectedType = QStringLiteral("U");
            break;
        }
        if (descriptor.fieldName != QStringLiteral("famp_attr_%1").arg(index)
            || fieldNames.at(fieldIndex) != descriptor.fieldName
            || fieldSizes.at(fieldIndex) != QStringLiteral("8")
            || fieldTypes.at(fieldIndex) != expectedType
            || fieldCounts.at(fieldIndex) != QStringLiteral("1"))
        {
            setFirstError(error, QStringLiteral("第 %1 个逐点属性字段定义无效。")
                                     .arg(index + 1));
            return AttributeMetadataStatus::Invalid;
        }

        famp::cloud::AttributeChannel channel;
        channel.name = descriptor.name;
        channel.unit = descriptor.unit;
        channel.type = descriptor.type;
        famp::cloud::AttributeSummary summary;
        summary.name = channel.name;
        summary.unit = channel.unit;
        summary.type = channel.type;
        QString validationError;
        const QString normalizedName = channel.name.trimmed().toCaseFolded();
        if (!famp::cloud::validateAttributeSummary(summary, &validationError)
            || normalizedNames.contains(normalizedName))
        {
            setFirstError(
                error,
                validationError.isEmpty()
                    ? QStringLiteral("逐点属性名称重复。") : validationError);
            return AttributeMetadataStatus::Invalid;
        }
        normalizedNames.insert(normalizedName);
        switch (channel.type)
        {
        case famp::cloud::AttributeValueType::Float64:
            channel.floatingValues.reserve(perChannelReserve);
            break;
        case famp::cloud::AttributeValueType::SignedInteger:
            channel.signedValues.reserve(perChannelReserve);
            break;
        case famp::cloud::AttributeValueType::UnsignedInteger:
            channel.unsignedValues.reserve(perChannelReserve);
            break;
        }
        channels.append(std::move(channel));
    }

    qint64 rowsRead = 0;
    while (rowsRead < pointCount && !file.atEnd())
    {
        const QByteArray rawLine = file.readLine(64 * 1024);
        if (!rawLine.endsWith('\n') && !file.atEnd())
        {
            setFirstError(error, QStringLiteral("PCD 数据行过长。"));
            return AttributeMetadataStatus::Invalid;
        }
        const QString line = QString::fromLatin1(rawLine).trimmed();
        if (line.isEmpty())
            continue;
        const QStringList values = splitPcdLine(line);
        if (values.size() != expectedFieldCount)
        {
            setFirstError(error, QStringLiteral("PCD 数据列数与属性定义不一致。"));
            return AttributeMetadataStatus::Invalid;
        }
        for (int index = 0; index < channels.size(); ++index)
        {
            auto& channel = channels[index];
            const QString& token = values.at(4 + index);
            bool ok = false;
            switch (channel.type)
            {
            case famp::cloud::AttributeValueType::Float64:
            {
                double value = 0.0;
                ok = parseFloatingAttribute(token, value);
                if (ok)
                    channel.floatingValues.append(value);
                break;
            }
            case famp::cloud::AttributeValueType::SignedInteger:
            {
                const qlonglong value = token.toLongLong(&ok, 10);
                if (ok)
                    channel.signedValues.append(value);
                break;
            }
            case famp::cloud::AttributeValueType::UnsignedInteger:
            {
                const qulonglong value = token.toULongLong(&ok, 10);
                if (ok)
                    channel.unsignedValues.append(value);
                break;
            }
            }
            if (!ok)
            {
                setFirstError(
                    error,
                    QStringLiteral("第 %1 行的逐点属性 %2 数值无效。")
                        .arg(rowsRead + 1)
                        .arg(channel.name));
                return AttributeMetadataStatus::Invalid;
            }
        }
        ++rowsRead;
    }
    if (rowsRead != pointCount)
    {
        setFirstError(error, QStringLiteral("PCD 属性数据行数少于 POINTS 声明。"));
        return AttributeMetadataStatus::Invalid;
    }
    while (!file.atEnd())
    {
        const QByteArray rawLine = file.readLine(64 * 1024);
        if (!QString::fromLatin1(rawLine).trimmed().isEmpty())
        {
            setFirstError(error, QStringLiteral("PCD 属性数据行数多于 POINTS 声明。"));
            return AttributeMetadataStatus::Invalid;
        }
    }

    famp::cloud::CloudAttributes attributes;
    for (auto& channel : channels)
    {
        QString validationError;
        if (!attributes.insert(std::move(channel), pointCount, &validationError))
        {
            setFirstError(error, validationError);
            return AttributeMetadataStatus::Invalid;
        }
    }
    if (attributes.size() != declaredAttributeCount)
    {
        setFirstError(error, QStringLiteral("逐点属性名称重复。"));
        return AttributeMetadataStatus::Invalid;
    }
    parsed.attributes = std::move(attributes);
    parsed.pointCount = pointCount;
    return AttributeMetadataStatus::Valid;
}

SpatialMetadataStatus readEmbeddedSpatial(
    const QString& path,
    famp::cloud::SpatialReference& spatial)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return SpatialMetadataStatus::NotPresent;
    bool markerPresent = false;
    bool markerValid = false;
    bool haveOrigin = false;
    bool haveTransform = false;
    famp::cloud::SpatialReference candidate;
    qint64 headerBytes = 0;
    while (!file.atEnd())
    {
        const QByteArray rawLine = file.readLine(64 * 1024);
        headerBytes += rawLine.size();
        if (headerBytes > kMaxHeaderBytes
            || (!rawLine.endsWith('\n') && !file.atEnd()))
        {
            return markerPresent
                ? SpatialMetadataStatus::Invalid
                : SpatialMetadataStatus::NotPresent;
        }
        const QString line = QString::fromLatin1(rawLine).trimmed();
        if (line.startsWith(QStringLiteral("DATA "), Qt::CaseInsensitive))
            break;
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (line.startsWith(QStringLiteral("# FAMP_SPATIAL_REFERENCE")))
        {
            markerPresent = true;
            markerValid = fields.size() == 3
                && fields.at(0) == QStringLiteral("#")
                && fields.at(1) == QStringLiteral("FAMP_SPATIAL_REFERENCE")
                && fields.at(2) == QStringLiteral("1");
        }
        else if (fields.size() == 5
                 && fields.at(0) == QStringLiteral("#")
                 && fields.at(1) == QStringLiteral("FAMP_ORIGIN"))
        {
            haveOrigin = true;
            for (int index = 0; index < 3; ++index)
            {
                bool ok = false;
                const double value = fields.at(index + 2).toDouble(&ok);
                if (!ok || !std::isfinite(value))
                    return SpatialMetadataStatus::Invalid;
                candidate.origin[static_cast<std::size_t>(index)] = value;
            }
        }
        else if (fields.size() == 18
                 && fields.at(0) == QStringLiteral("#")
                 && fields.at(1) == QStringLiteral("FAMP_TRANSFORM"))
        {
            haveTransform = true;
            for (int index = 0; index < 16; ++index)
            {
                bool ok = false;
                const double value = fields.at(index + 2).toDouble(&ok);
                if (!ok || !std::isfinite(value))
                    return SpatialMetadataStatus::Invalid;
                candidate.transform[static_cast<std::size_t>(index)] = value;
            }
        }
    }
    if (!markerPresent)
        return SpatialMetadataStatus::NotPresent;
    if (!markerValid || !haveOrigin || !haveTransform)
        return SpatialMetadataStatus::Invalid;
    spatial = candidate;
    return SpatialMetadataStatus::Valid;
}

bool sanitizeCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud)
{
    if (!cloud)
        return false;
    cloud->erase(
        std::remove_if(cloud->begin(), cloud->end(),
                       [](const pcl::PointXYZRGB& point) {
                           return !pcl::isFinite(point);
                       }),
        cloud->end());
    if (cloud->empty())
        return false;
    cloud->width = static_cast<std::uint32_t>(cloud->size());
    cloud->height = 1;
    cloud->is_dense = true;
    return true;
}

bool pcdHeaderHasColorField(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return true;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream stream(line);
        std::string key;
        stream >> key;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (key == "fields")
        {
            std::string field;
            while (stream >> field)
            {
                std::transform(field.begin(), field.end(), field.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (field == "rgb" || field == "rgba")
                {
                    return true;
                }
            }
            return false;
        }

        if (key == "data")
        {
            break;
        }
    }

    return true;
}

bool loadXyzPcdAsRgb(const std::string& path,
                     pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud)
{
    pcl::PointCloud<pcl::PointXYZ> xyzCloud;
    if (pcl::io::loadPCDFile(path, xyzCloud) != 0)
    {
        return false;
    }

    auto converted = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
    converted->reserve(xyzCloud.size());
    converted->width = xyzCloud.width;
    converted->height = xyzCloud.height;
    converted->is_dense = xyzCloud.is_dense;

    for (const auto& sourcePoint : xyzCloud)
    {
        pcl::PointXYZRGB targetPoint;
        targetPoint.x = sourcePoint.x;
        targetPoint.y = sourcePoint.y;
        targetPoint.z = sourcePoint.z;
        targetPoint.r = kDefaultRed;
        targetPoint.g = kDefaultGreen;
        targetPoint.b = kDefaultBlue;
        converted->push_back(targetPoint);
    }

    outCloud = converted;
    return true;
}

bool loadPcdFromStdPath(const std::string& path,
                        pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud)
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr candidate;
    bool loaded = false;
    if (!pcdHeaderHasColorField(path))
    {
        loaded = loadXyzPcdAsRgb(path, candidate);
    }
    else
    {
        auto rgbCloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
            new pcl::PointCloud<pcl::PointXYZRGB>);
        if (pcl::io::loadPCDFile(path, *rgbCloud) == 0)
        {
            candidate = rgbCloud;
            loaded = true;
        }
        else
        {
            loaded = loadXyzPcdAsRgb(path, candidate);
        }
    }
    if (!loaded || !sanitizeCloud(candidate))
        return false;
    outCloud = candidate;
    return true;
}

bool copyFileWithQt(const QString& sourcePath, const QString& targetPath, QString* errorMessage)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to open source PCD file: %1 (%2)")
                                .arg(sourcePath, source.errorString());
        }
        return false;
    }

    QFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to create temporary PCD file: %1 (%2)")
                                .arg(targetPath, target.errorString());
        }
        return false;
    }

    while (!source.atEnd())
    {
        const QByteArray chunk = source.read(kCopyBufferSize);
        if (chunk.isEmpty() && source.error() != QFile::NoError)
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Failed to read source PCD file: %1 (%2)")
                                    .arg(sourcePath, source.errorString());
            }
            return false;
        }

        if (target.write(chunk) != chunk.size())
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Failed to write temporary PCD file: %1 (%2)")
                                    .arg(targetPath, target.errorString());
            }
            return false;
        }
    }

    target.close();
    if (target.error() != QFile::NoError)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to finalize temporary PCD file: %1 (%2)")
                                .arg(targetPath, target.errorString());
        }
        return false;
    }

    return true;
}
}

bool loadPcdAsRgb(const std::string& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  std::string* errorMessage)
{
    if (loadPcdFromStdPath(path, outCloud))
    {
        return true;
    }

    if (errorMessage)
    {
        *errorMessage = "Failed to load PCD file: " + path;
    }
    return false;
}

bool loadPcdAsRgb(const QString& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  QString* errorMessage,
                  famp::cloud::SpatialReference* embeddedSpatial,
                  bool* hasEmbeddedSpatial,
                  famp::cloud::CloudAttributes* embeddedAttributes)
{
    ParsedPcdAttributes parsedAttributes;
    QString attributeError;
    const AttributeMetadataStatus attributeMetadata =
        readEmbeddedAttributes(path, parsedAttributes, attributeError);
    if (attributeMetadata == AttributeMetadataStatus::Invalid)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral(
                "PCD 中的 FAMP 逐点属性元数据无效：%1\n%2")
                                .arg(path, attributeError);
        }
        return false;
    }

    famp::cloud::SpatialReference parsedSpatial;
    const SpatialMetadataStatus metadata = readEmbeddedSpatial(path, parsedSpatial);
    if (metadata == SpatialMetadataStatus::Invalid)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("PCD 中的 FAMP 空间参考元数据无效：%1").arg(path);
        return false;
    }

    auto commitLoaded = [&](const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& loaded) {
        if (attributeMetadata == AttributeMetadataStatus::Valid
            && parsedAttributes.pointCount
                != static_cast<qint64>(loaded->size()))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral(
                    "PCD 逐点属性数量 %1 与有效点数 %2 不一致：%3")
                                    .arg(parsedAttributes.pointCount)
                                    .arg(loaded->size())
                                    .arg(path);
            }
            return false;
        }
        outCloud = loaded;
        if (hasEmbeddedSpatial)
            *hasEmbeddedSpatial = metadata == SpatialMetadataStatus::Valid;
        if (embeddedSpatial && metadata == SpatialMetadataStatus::Valid)
            *embeddedSpatial = parsedSpatial;
        if (embeddedAttributes)
        {
            if (attributeMetadata == AttributeMetadataStatus::Valid)
                *embeddedAttributes = parsedAttributes.attributes;
            else
                embeddedAttributes->clear();
        }
        if (errorMessage)
            errorMessage->clear();
        return true;
    };

    const std::string utf8Path = qstr2str(path);
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loadedCloud;
    if (loadPcdFromStdPath(utf8Path, loadedCloud))
    {
        return commitLoaded(loadedCloud);
    }

    // On Windows, PCL's narrow file API may fail on non-ASCII paths. Retry via
    // a temporary ASCII file while keeping the user's original file untouched.
    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/FAMP-pcd-XXXXXX.pcd"));
    tempFile.setAutoRemove(true);
    if (tempFile.open())
    {
        const QString tempPath = tempFile.fileName();
        tempFile.close();

        QString copyError;
        if (copyFileWithQt(path, tempPath, &copyError))
        {
            pcl::PointCloud<pcl::PointXYZRGB>::Ptr temporaryCloud;
            const bool loaded = loadPcdFromStdPath(
                qstr2str(tempPath), temporaryCloud);
            QFile::remove(tempPath);
            if (loaded)
            {
                return commitLoaded(temporaryCloud);
            }
        }
        else if (errorMessage)
        {
            *errorMessage = copyError;
        }
    }

    if (errorMessage && errorMessage->isEmpty())
    {
        *errorMessage = QStringLiteral("Failed to load PCD file: %1").arg(path);
    }
    return false;
}
