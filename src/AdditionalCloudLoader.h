#pragma once

#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>

bool loadPlyAsRgb(
    const QString& path,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool loadXyzTextAsRgb(
    const QString& path,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});
