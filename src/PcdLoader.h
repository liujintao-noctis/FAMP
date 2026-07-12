/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Description: PCD loading helpers
 *****************************************************************/

#pragma once

#include "CloudCoordinates.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>

#include <string>

bool loadPcdAsRgb(const std::string& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  std::string* errorMessage = nullptr);

bool loadPcdAsRgb(const QString& path,
                  pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloud,
                  QString* errorMessage = nullptr,
                  famp::cloud::SpatialReference* embeddedSpatial = nullptr,
                  bool* hasEmbeddedSpatial = nullptr);
