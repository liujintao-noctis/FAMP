#pragma once

#include <QString>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

bool loadLasAsRgb(const QString& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  QString* errorMessage = nullptr);
