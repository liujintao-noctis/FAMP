#pragma once

#include "CloudCoordinates.h"
#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>

#include <functional>

namespace famp::cloud
{
using ReprojectionProgress = std::function<void(double)>;

struct ReprojectionResult
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr points;
    SpatialReference spatial;
    QString sourceCrs;
    QString targetCrs;
    QString error;
    bool cancelled = false;

    bool succeeded() const
    {
        return points && !points->empty() && error.isEmpty() && !cancelled;
    }
};

ReprojectionResult reproject(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    const SpatialReference& inputSpatial,
    const QString& sourceCrs,
    const QString& targetCrs,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const ReprojectionProgress& reportProgress = {});
}
