#pragma once

#include "CloudCoordinates.h"
#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <Eigen/Core>
#include <QString>
#include <QVector>

#include <cstddef>

namespace famp::registration
{
struct Options
{
    int maximumIterations = 60;
    double maximumCorrespondenceDistance = 1.0;
    double samplingVoxelSizeMeters = 0.0;
    double transformationEpsilon = 1.0e-8;
    double fitnessEpsilon = 1.0e-8;
    double minimumOverlapRatio = 0.25;
};

struct Result
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
    Eigen::Matrix4d sourceToTargetFrame = Eigen::Matrix4d::Identity();
    Eigen::Matrix4d combinedTransform = Eigen::Matrix4d::Identity();
    QString outputPath;
    QString error;
    std::size_t sourcePointCount = 0;
    std::size_t targetPointCount = 0;
    std::size_t registrationSourcePointCount = 0;
    std::size_t registrationTargetPointCount = 0;
    std::size_t overlappingSourcePointCount = 0;
    std::size_t overlappingTargetPointCount = 0;
    std::size_t outputPointCount = 0;
    QVector<qint64> sourceIndices;
    double fitnessScore = 0.0;
    double sourceOverlapRatio = 0.0;
    double targetOverlapRatio = 0.0;
    double overlapRatio = 0.0;
    bool converged = false;
    bool cancelled = false;

    bool succeeded() const
    {
        return converged && cloud && !cloud->empty() && error.isEmpty();
    }
};

bool validateOptions(const Options& options, QString* errorMessage = nullptr);

Result align(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
             const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
             const Options& options,
             const famp::tasks::CancellationCheck& shouldCancel = {});

// Converts the source from its local display frame into the target local
// frame before ICP. The returned cloud and transforms are expressed in the
// target frame, which avoids aligning unrelated local origins.
Result alignInTargetFrame(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
    const famp::cloud::SpatialReference& sourceSpatial,
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
    const famp::cloud::SpatialReference& targetSpatial,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel = {});

Result alignAndSave(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
    const Options& options,
    const QString& requestedOutputPath,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const famp::cloud::SpatialReference* spatial = nullptr);
}
