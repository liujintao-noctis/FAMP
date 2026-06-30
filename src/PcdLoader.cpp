/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Description: PCD loading helpers
 *****************************************************************/

#include "PcdLoader.h"
#include "QstringAndStringConvert.h"

#include <pcl/io/pcd_io.h>

#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace
{
constexpr std::uint8_t kDefaultRed = 255;
constexpr std::uint8_t kDefaultGreen = 255;
constexpr std::uint8_t kDefaultBlue = 255;
constexpr qint64 kCopyBufferSize = 1024 * 1024;

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
    if (!pcdHeaderHasColorField(path))
    {
        return loadXyzPcdAsRgb(path, outCloud);
    }

    auto rgbCloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
    if (pcl::io::loadPCDFile(path, *rgbCloud) == 0)
    {
        outCloud = rgbCloud;
        return true;
    }

    return loadXyzPcdAsRgb(path, outCloud);
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
                  QString* errorMessage)
{
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
        QFile::remove(tempPath);

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
