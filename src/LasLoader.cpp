#include "LasLoader.h"
#include "QstringAndStringConvert.h"

#include <lasreader.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace
{
constexpr std::uint64_t MaxPreallocatedPoints = 1'000'000;
constexpr qint64 CopyBufferSize = 1024 * 1024;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool copyIntoTemporaryFile(const QString& sourcePath,
                           QTemporaryFile& target)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    while (!source.atEnd())
    {
        const QByteArray chunk = source.read(CopyBufferSize);
        if ((chunk.isEmpty() && source.error() != QFile::NoError)
            || target.write(chunk) != chunk.size())
        {
            return false;
        }
    }
    if (!target.flush() || target.error() != QFile::NoError)
        return false;
    target.close();
    return true;
}

bool loadLasFromStdPath(const std::string& path,
                        pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                        std::array<double, 3>* origin,
                        famp::cloud::CloudAttributes* attributes)
{
    LASreadOpener opener;
    std::vector<char> pathBuffer(path.begin(), path.end());
    pathBuffer.push_back('\0');
    opener.set_file_name(pathBuffer.data());

    auto closeReader = [](LASreader* reader) {
        if (reader)
        {
            reader->close();
            delete reader;
        }
    };
    std::unique_ptr<LASreader, decltype(closeReader)> reader(opener.open(), closeReader);
    if (!reader)
        return false;

    const double centerX = reader->get_min_x()
        + (reader->get_max_x() - reader->get_min_x()) / 2.0;
    const double centerY = reader->get_min_y()
        + (reader->get_max_y() - reader->get_min_y()) / 2.0;
    const double centerZ = reader->get_min_z()
        + (reader->get_max_z() - reader->get_min_z()) / 2.0;
    if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(centerZ))
        return false;

    auto loaded = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    const std::uint64_t declaredPointCount = std::max<std::uint64_t>(
        reader->header.number_of_point_records,
        reader->header.extended_number_of_point_records);
    loaded->points.reserve(static_cast<std::size_t>(
        std::min(declaredPointCount, MaxPreallocatedPoints)));

    const bool collectAttributes = attributes != nullptr;
    const int attributeReserve = static_cast<int>(std::min<std::uint64_t>(
        declaredPointCount,
        std::min<std::uint64_t>(MaxPreallocatedPoints,
                                static_cast<std::uint64_t>(
                                    std::numeric_limits<int>::max()))));
    famp::cloud::AttributeChannel intensity;
    intensity.name = QStringLiteral("intensity");
    intensity.unit = QStringLiteral("raw");
    intensity.type = famp::cloud::AttributeValueType::UnsignedInteger;
    if (collectAttributes)
        intensity.unsignedValues.reserve(attributeReserve);
    famp::cloud::AttributeChannel classification;
    classification.name = QStringLiteral("classification");
    classification.type = famp::cloud::AttributeValueType::UnsignedInteger;
    if (collectAttributes)
        classification.unsignedValues.reserve(attributeReserve);
    famp::cloud::AttributeChannel returnNumber;
    returnNumber.name = QStringLiteral("return_number");
    returnNumber.type = famp::cloud::AttributeValueType::UnsignedInteger;
    if (collectAttributes)
        returnNumber.unsignedValues.reserve(attributeReserve);
    famp::cloud::AttributeChannel numberOfReturns;
    numberOfReturns.name = QStringLiteral("number_of_returns");
    numberOfReturns.type = famp::cloud::AttributeValueType::UnsignedInteger;
    if (collectAttributes)
        numberOfReturns.unsignedValues.reserve(attributeReserve);
    famp::cloud::AttributeChannel scanAngle;
    scanAngle.name = QStringLiteral("scan_angle");
    scanAngle.unit = QStringLiteral("degree");
    scanAngle.type = famp::cloud::AttributeValueType::Float64;
    if (collectAttributes)
        scanAngle.floatingValues.reserve(attributeReserve);
    famp::cloud::AttributeChannel userData;
    userData.name = QStringLiteral("user_data");
    userData.type = famp::cloud::AttributeValueType::UnsignedInteger;
    if (collectAttributes)
        userData.unsignedValues.reserve(attributeReserve);
    famp::cloud::AttributeChannel pointSourceId;
    pointSourceId.name = QStringLiteral("point_source_id");
    pointSourceId.type = famp::cloud::AttributeValueType::UnsignedInteger;
    if (collectAttributes)
        pointSourceId.unsignedValues.reserve(attributeReserve);
    famp::cloud::AttributeChannel gpsTime;
    gpsTime.name = QStringLiteral("gps_time");
    gpsTime.unit = QStringLiteral("s");
    gpsTime.type = famp::cloud::AttributeValueType::Float64;
    if (collectAttributes)
        gpsTime.floatingValues.reserve(attributeReserve);
    const bool hasGpsTime = reader->point.have_gps_time;

    while (reader->read_point())
    {
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(reader->point.get_x() - centerX);
        point.y = static_cast<float>(reader->point.get_y() - centerY);
        point.z = static_cast<float>(reader->point.get_z() - centerZ);
        if (!std::isfinite(point.x)
            || !std::isfinite(point.y)
            || !std::isfinite(point.z))
        {
            continue;
        }
        point.r = static_cast<std::uint8_t>(reader->point.get_R() >> 8);
        point.g = static_cast<std::uint8_t>(reader->point.get_G() >> 8);
        point.b = static_cast<std::uint8_t>(reader->point.get_B() >> 8);
        loaded->points.push_back(point);

        if (collectAttributes)
        {
            const bool extended = reader->point.is_extended_point_type();
            intensity.unsignedValues.append(reader->point.get_intensity());
            classification.unsignedValues.append(
                extended ? reader->point.get_extended_classification()
                         : reader->point.get_classification());
            returnNumber.unsignedValues.append(
                extended ? reader->point.get_extended_return_number()
                         : reader->point.get_return_number());
            numberOfReturns.unsignedValues.append(
                extended ? reader->point.get_extended_number_of_returns()
                         : reader->point.get_number_of_returns());
            scanAngle.floatingValues.append(reader->point.get_scan_angle());
            userData.unsignedValues.append(reader->point.get_user_data());
            pointSourceId.unsignedValues.append(
                reader->point.get_point_source_ID());
            if (hasGpsTime)
                gpsTime.floatingValues.append(reader->point.get_gps_time());
        }
    }

    if (loaded->empty())
        return false;

    loaded->width = static_cast<std::uint32_t>(loaded->points.size());
    loaded->height = 1;
    loaded->is_dense = true;

    famp::cloud::CloudAttributes loadedAttributes;
    if (collectAttributes)
    {
        const qint64 pointCount = static_cast<qint64>(loaded->size());
        if (!loadedAttributes.insert(std::move(intensity), pointCount)
            || !loadedAttributes.insert(std::move(classification), pointCount)
            || !loadedAttributes.insert(std::move(returnNumber), pointCount)
            || !loadedAttributes.insert(std::move(numberOfReturns), pointCount)
            || !loadedAttributes.insert(std::move(scanAngle), pointCount)
            || !loadedAttributes.insert(std::move(userData), pointCount)
            || !loadedAttributes.insert(std::move(pointSourceId), pointCount))
        {
            return false;
        }
        if (hasGpsTime
            && !loadedAttributes.insert(std::move(gpsTime), pointCount))
        {
            return false;
        }
    }

    outCloud = loaded;
    if (origin)
        *origin = {centerX, centerY, centerZ};
    if (attributes)
        *attributes = std::move(loadedAttributes);
    return true;
}
}

bool loadLasAsRgb(const QString& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  QString* errorMessage,
                  std::array<double, 3>* origin,
                  famp::cloud::CloudAttributes* attributes)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        setError(errorMessage, QStringLiteral("LAS/LAZ 文件不存在：%1").arg(path));
        return false;
    }

    if (loadLasFromStdPath(
            qstr2str(fileInfo.absoluteFilePath()), outCloud, origin, attributes))
    {
        if (errorMessage)
            errorMessage->clear();
        return true;
    }

    // LAStools uses a narrow path API. On Windows, retry through a temporary
    // ASCII-named copy so paths containing Chinese or other Unicode text work.
    const QString suffix = fileInfo.suffix().compare(
                               QStringLiteral("laz"), Qt::CaseInsensitive) == 0
        ? QStringLiteral(".laz") : QStringLiteral(".las");
    QTemporaryFile temporaryFile(
        QDir::tempPath() + QStringLiteral("/FAMP-las-XXXXXX") + suffix);
    temporaryFile.setAutoRemove(true);
    if (!temporaryFile.open())
    {
        setError(errorMessage,
                 QStringLiteral("无法创建 LAS/LAZ 临时文件：%1")
                     .arg(temporaryFile.errorString()));
        return false;
    }

    const QString temporaryPath = temporaryFile.fileName();
    if (!copyIntoTemporaryFile(
            fileInfo.absoluteFilePath(), temporaryFile))
    {
        setError(errorMessage,
                 QStringLiteral("无法读取 LAS/LAZ 文件：%1").arg(path));
        return false;
    }

    const bool loaded = loadLasFromStdPath(
        qstr2str(temporaryPath), outCloud, origin, attributes);
    if (!loaded)
    {
        setError(errorMessage,
                 QStringLiteral("LAS/LAZ 文件无效、为空或不受支持：%1").arg(path));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}
