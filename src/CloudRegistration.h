#pragma once

#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <Eigen/Core>
#include <QString>

#include <cstddef>

namespace famp::registration
{
struct Options
{
    int maximumIterations = 60;
    double maximumCorrespondenceDistance = 1.0;
    double transformationEpsilon = 1.0e-8;
    double fitnessEpsilon = 1.0e-8;
};

struct Result
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
    QString outputPath;
    QString error;
    std::size_t sourcePointCount = 0;
    std::size_t targetPointCount = 0;
    std::size_t outputPointCount = 0;
    double fitnessScore = 0.0;
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

Result alignAndSave(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
    const Options& options,
    const QString& requestedOutputPath,
    const famp::tasks::CancellationCheck& shouldCancel = {});
}
