#pragma once

#include "CloudCoordinates.h"
#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>

#include <cstddef>

namespace famp::processing
{
enum class Method
{
    VoxelDownsample,
    StatisticalOutlierRemoval
};

struct Options
{
    Method method = Method::VoxelDownsample;
    double voxelLeafSizeMeters = 0.01;
    int meanNeighbors = 20;
    double standardDeviationMultiplier = 1.0;
};

struct Result
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    QString outputPath;
    QString error;
    std::size_t inputPointCount = 0;
    std::size_t finitePointCount = 0;
    std::size_t outputPointCount = 0;
    bool cancelled = false;

    bool succeeded() const
    {
        return cloud && !cloud->empty() && error.isEmpty();
    }
};

bool validateOptions(const Options& options,
                     std::size_t inputPointCount,
                     QString* errorMessage = nullptr);

Result process(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
               const Options& options,
               const famp::tasks::CancellationCheck& shouldCancel = {});

Result processAndSave(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    const Options& options,
    const QString& requestedOutputPath,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const famp::cloud::SpatialReference* spatial = nullptr);
}
