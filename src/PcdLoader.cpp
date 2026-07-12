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
#include <QTemporaryFile>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cmath>

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
                  bool* hasEmbeddedSpatial)
{
    famp::cloud::SpatialReference parsedSpatial;
    const SpatialMetadataStatus metadata = readEmbeddedSpatial(path, parsedSpatial);
    if (metadata == SpatialMetadataStatus::Invalid)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("PCD 中的 FAMP 空间参考元数据无效：%1").arg(path);
        return false;
    }
    if (hasEmbeddedSpatial)
        *hasEmbeddedSpatial = metadata == SpatialMetadataStatus::Valid;
    if (embeddedSpatial && metadata == SpatialMetadataStatus::Valid)
        *embeddedSpatial = parsedSpatial;
    const std::string utf8Path = qstr2str(path);
    if (loadPcdFromStdPath(utf8Path, outCloud))
    {
        return true;
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
            const bool loaded = loadPcdFromStdPath(qstr2str(tempPath), outCloud);
            QFile::remove(tempPath);
            if (loaded)
            {
                return true;
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
