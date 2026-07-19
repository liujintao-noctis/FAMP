#pragma once

#include "CloudAttributes.h"

#include <QString>

#include <array>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

bool loadLasAsRgb(const QString& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  QString* errorMessage = nullptr,
                  std::array<double, 3>* origin = nullptr,
                  famp::cloud::CloudAttributes* attributes = nullptr);
