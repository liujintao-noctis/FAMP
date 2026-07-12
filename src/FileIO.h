#pragma once

#include "CloudCoordinates.h"

#include <QImage>
#include <QString>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

namespace famp::io
{
QString pathWithRequiredSuffix(const QString& path, const QString& suffix);

bool saveImageAtomically(const QString& path,
                         const QImage& image,
                         const char* format,
                         int quality,
                         QString* errorMessage = nullptr);

bool savePcdAsciiAtomically(
    const QString& path,
    const pcl::PointCloud<pcl::PointXYZRGB>& cloud,
    QString* errorMessage = nullptr,
    const famp::cloud::SpatialReference* spatial = nullptr);
}
