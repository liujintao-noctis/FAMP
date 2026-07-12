/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Description: Thread-safe point-cloud loading service
 *****************************************************************/

#pragma once

#include "CloudCoordinates.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>

namespace famp::cloud
{
struct LoadResult
{
    QString path;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr displayCloud;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr sourceCloud;
    QString error;
    bool sourceWasPcd = false;
    SpatialReference spatial;

    bool succeeded() const
    {
        return displayCloud && !displayCloud->empty() && error.isEmpty();
    }
};

bool validatePath(const QString& requestedPath,
                  QString* normalizedPath,
                  QString* errorMessage = nullptr);

LoadResult load(const QString& requestedPath);
}
