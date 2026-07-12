#include "CloudRegistration.h"

#include "FileIO.h"

#include <pcl/common/point_tests.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/registration/icp.h>

#include <cmath>
#include <exception>

namespace famp::registration
{
namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool cancel(Result& result,
            const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!famp::tasks::isCancellationRequested(shouldCancel))
        return false;
    result.cloud.reset();
    result.outputPointCount = 0;
    result.converged = false;
    result.cancelled = true;
    result.error = QStringLiteral("点云配准已取消。");
    return true;
}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr finiteCopy(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    Result& result,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    auto output = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    output->reserve(input->size());
    std::size_t index = 0;
    for (const auto& point : input->points)
    {
        if ((index++ & 0x0fffU) == 0U && cancel(result, shouldCancel))
            return {};
        if (pcl::isFinite(point))
            output->push_back(point);
    }
    output->width = static_cast<std::uint32_t>(output->size());
    output->height = 1;
    output->is_dense = true;
    return output;
}
}

bool validateOptions(const Options& options, QString* errorMessage)
{
    if (options.maximumIterations < 1 || options.maximumIterations > 10000)
    {
        setError(errorMessage, QStringLiteral("最大迭代次数必须在 1 到 10000 之间。"));
        return false;
    }
    if (!std::isfinite(options.maximumCorrespondenceDistance)
        || options.maximumCorrespondenceDistance <= 0.0
        || options.maximumCorrespondenceDistance > 100000.0)
    {
        setError(errorMessage, QStringLiteral("最大对应距离必须大于 0 且不超过 100000 米。"));
        return false;
    }
    if (!std::isfinite(options.samplingVoxelSizeMeters)
        || options.samplingVoxelSizeMeters < 0.0
        || options.samplingVoxelSizeMeters > 1000.0)
    {
        setError(errorMessage,
                 QStringLiteral("配准体素边长必须为 0（不降采样）或不超过 1000 米。"));
        return false;
    }
    if (!std::isfinite(options.transformationEpsilon)
        || options.transformationEpsilon <= 0.0
        || options.transformationEpsilon > 1.0)
    {
        setError(errorMessage, QStringLiteral("变换收敛阈值必须大于 0 且不超过 1。"));
        return false;
    }
    if (!std::isfinite(options.fitnessEpsilon)
        || options.fitnessEpsilon <= 0.0 || options.fitnessEpsilon > 1.0)
    {
        setError(errorMessage, QStringLiteral("适应度收敛阈值必须大于 0 且不超过 1。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

Result align(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
             const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
             const Options& options,
             const famp::tasks::CancellationCheck& shouldCancel)
{
    Result result;
    result.sourcePointCount = source ? source->size() : 0;
    result.targetPointCount = target ? target->size() : 0;
    if (!source || !target)
    {
        result.error = QStringLiteral("源点云或目标点云为空指针。");
        return result;
    }
    if (source.get() == target.get())
    {
        result.error = QStringLiteral("源点云和目标点云必须不同。");
        return result;
    }
    if (!validateOptions(options, &result.error) || cancel(result, shouldCancel))
        return result;

    auto finiteSource = finiteCopy(source, result, shouldCancel);
    if (result.cancelled)
        return result;
    auto finiteTarget = finiteCopy(target, result, shouldCancel);
    if (result.cancelled)
        return result;
    if (!finiteSource || !finiteTarget
        || finiteSource->size() < 3 || finiteTarget->size() < 3)
    {
        result.error = QStringLiteral("ICP 配准要求源点云和目标点云各至少包含 3 个有效点。");
        return result;
    }

    try
    {
        auto registrationSource = finiteSource;
        auto registrationTarget = finiteTarget;
        if (options.samplingVoxelSizeMeters > 0.0)
        {
            const float leaf = static_cast<float>(options.samplingVoxelSizeMeters);
            auto sampledSource = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
                new pcl::PointCloud<pcl::PointXYZRGB>);
            auto sampledTarget = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
                new pcl::PointCloud<pcl::PointXYZRGB>);
            pcl::VoxelGrid<pcl::PointXYZRGB> sourceFilter;
            sourceFilter.setInputCloud(finiteSource);
            sourceFilter.setLeafSize(leaf, leaf, leaf);
            sourceFilter.filter(*sampledSource);
            if (cancel(result, shouldCancel))
                return result;
            pcl::VoxelGrid<pcl::PointXYZRGB> targetFilter;
            targetFilter.setInputCloud(finiteTarget);
            targetFilter.setLeafSize(leaf, leaf, leaf);
            targetFilter.filter(*sampledTarget);
            if (cancel(result, shouldCancel))
                return result;
            registrationSource = sampledSource;
            registrationTarget = sampledTarget;
        }
        result.registrationSourcePointCount = registrationSource->size();
        result.registrationTargetPointCount = registrationTarget->size();
        if (registrationSource->size() < 3 || registrationTarget->size() < 3)
        {
            result.error = QStringLiteral(
                "配准降采样后有效点少于 3 个，请减小体素边长或关闭降采样。");
            return result;
        }

        pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> icp;
        icp.setInputSource(registrationSource);
        icp.setInputTarget(registrationTarget);
        icp.setMaximumIterations(options.maximumIterations);
        icp.setMaxCorrespondenceDistance(options.maximumCorrespondenceDistance);
        icp.setTransformationEpsilon(options.transformationEpsilon);
        icp.setEuclideanFitnessEpsilon(options.fitnessEpsilon);
        pcl::PointCloud<pcl::PointXYZRGB> preview;
        icp.align(preview);
        if (cancel(result, shouldCancel))
            return result;
        result.converged = icp.hasConverged();
        result.fitnessScore = icp.getFitnessScore(
            options.maximumCorrespondenceDistance);
        result.transform = icp.getFinalTransformation();
        const double maximumAcceptedFitness =
            options.maximumCorrespondenceDistance
            * options.maximumCorrespondenceDistance;
        if (!result.converged || !std::isfinite(result.fitnessScore)
            || result.fitnessScore > maximumAcceptedFitness
            || !result.transform.allFinite())
        {
            result.converged = false;
            result.error = QStringLiteral(
                "ICP 未获得有效重叠结果，请检查初始位置、最大对应距离或降采样参数。");
            return result;
        }
        result.cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
        pcl::transformPointCloud(*finiteSource, *result.cloud, result.transform);
        result.cloud->width = static_cast<std::uint32_t>(result.cloud->size());
        result.cloud->height = 1;
        result.cloud->is_dense = true;
        result.outputPointCount = result.cloud->size();
    }
    catch (const std::exception& exception)
    {
        result.cloud.reset();
        result.converged = false;
        result.error = QStringLiteral("点云配准失败：%1")
                           .arg(QString::fromLocal8Bit(exception.what()));
    }
    catch (...)
    {
        result.cloud.reset();
        result.converged = false;
        result.error = QStringLiteral("点云配准发生未知错误。");
    }
    return result;
}

Result alignAndSave(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
    const Options& options,
    const QString& requestedOutputPath,
    const famp::tasks::CancellationCheck& shouldCancel,
    const famp::cloud::SpatialReference* spatial)
{
    Result result = align(source, target, options, shouldCancel);
    if (!result.succeeded() || cancel(result, shouldCancel))
        return result;
    result.outputPath = famp::io::pathWithRequiredSuffix(
        requestedOutputPath, QStringLiteral("pcd"));
    if (!famp::io::savePcdAsciiAtomically(
            result.outputPath, *result.cloud, &result.error, spatial))
    {
        result.cloud.reset();
        result.outputPointCount = 0;
        result.converged = false;
    }
    return result;
}
}
