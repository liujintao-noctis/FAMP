#pragma once

#include "CloudCoordinates.h"
#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>
#include <QVector>

#include <cstddef>

namespace famp::crop
{
struct Options
{
    double minimumX = -1.0;
    double maximumX = 1.0;
    double minimumY = -1.0;
    double maximumY = 1.0;
    double minimumZ = -1.0;
    double maximumZ = 1.0;
    bool keepInside = true;
};

struct Result
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    QString outputPath;
    QString error;
    std::size_t inputPointCount = 0;
    std::size_t outputPointCount = 0;
    QVector<qint64> sourceIndices;
    bool cancelled = false;

    bool succeeded() const
    {
        return cloud && !cloud->empty() && error.isEmpty();
    }
};

bool dataBounds(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    Options& bounds,
    QString* errorMessage = nullptr);

bool validateOptions(const Options& options,
                     QString* errorMessage = nullptr);

Result process(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel = {});

Result processAndSave(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    const Options& options,
    const QString& requestedOutputPath,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const famp::cloud::SpatialReference* spatial = nullptr);
}
