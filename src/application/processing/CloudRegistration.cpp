#include "CloudRegistration.h"

#include "FileIO.h"

#include <pcl/common/point_tests.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/registration/icp.h>

#include <Eigen/LU>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <vector>

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
    result.overlappingSourcePointCount = 0;
    result.overlappingTargetPointCount = 0;
    result.sourceOverlapRatio = 0.0;
    result.targetOverlapRatio = 0.0;
    result.overlapRatio = 0.0;
    result.sourceIndices.clear();
    result.converged = false;
    result.cancelled = true;
    result.error = QStringLiteral("点云配准已取消。");
    return true;
}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr finiteCopy(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    Result& result,
    const famp::tasks::CancellationCheck& shouldCancel,
    QVector<qint64>* sourceIndices = nullptr)
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
        {
            output->push_back(point);
            if (sourceIndices)
                sourceIndices->append(static_cast<qint64>(index - 1));
        }
    }
    output->width = static_cast<std::uint32_t>(output->size());
    output->height = 1;
    output->is_dense = true;
    return output;
}

bool spatialMatrix(const famp::cloud::SpatialReference& spatial,
                   Eigen::Matrix4d& result)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const double value = spatial.transform[static_cast<std::size_t>(
                row * 4 + column)];
            if (!std::isfinite(value))
                return false;
            result(row, column) = value;
        }
    }
    return std::all_of(
        spatial.origin.cbegin(), spatial.origin.cend(),
        [](double value) { return std::isfinite(value); });
}

bool sourceToTargetMatrix(
    const famp::cloud::SpatialReference& source,
    const famp::cloud::SpatialReference& target,
    Eigen::Matrix4d& result)
{
    Eigen::Matrix4d sourceTransform;
    Eigen::Matrix4d targetTransform;
    if (!spatialMatrix(source, sourceTransform)
        || !spatialMatrix(target, targetTransform))
    {
        return false;
    }
    const Eigen::FullPivLU<Eigen::Matrix4d> targetDecomposition(
        targetTransform);
    if (!targetDecomposition.isInvertible())
        return false;

    Eigen::Matrix4d addSourceOrigin = Eigen::Matrix4d::Identity();
    Eigen::Matrix4d subtractTargetOrigin = Eigen::Matrix4d::Identity();
    for (int axis = 0; axis < 3; ++axis)
    {
        addSourceOrigin(axis, 3) = source.origin[static_cast<std::size_t>(axis)];
        subtractTargetOrigin(axis, 3) =
            -target.origin[static_cast<std::size_t>(axis)];
    }
    result = subtractTargetOrigin
        * targetDecomposition.inverse()
        * sourceTransform
        * addSourceOrigin;
    return result.allFinite();
}

bool countOverlappingPoints(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& query,
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& reference,
    double maximumDistance,
    std::size_t& matchCount,
    Result& result,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    pcl::KdTreeFLANN<pcl::PointXYZRGB> tree;
    tree.setInputCloud(reference);
    const float maximumSquaredDistance = static_cast<float>(
        maximumDistance * maximumDistance);
    std::vector<int> neighborIndices(1);
    std::vector<float> squaredDistances(1);
    matchCount = 0;
    for (std::size_t index = 0; index < query->size(); ++index)
    {
        if ((index & 0x0fffU) == 0U && cancel(result, shouldCancel))
            return false;
        if (tree.nearestKSearch(
                query->points[index], 1, neighborIndices, squaredDistances) > 0
            && squaredDistances.front() <= maximumSquaredDistance)
        {
            ++matchCount;
        }
    }
    return true;
}

bool calculateBidirectionalOverlap(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& alignedSource,
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
    double maximumDistance,
    Result& result,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!countOverlappingPoints(
            alignedSource, target, maximumDistance,
            result.overlappingSourcePointCount, result, shouldCancel)
        || !countOverlappingPoints(
            target, alignedSource, maximumDistance,
            result.overlappingTargetPointCount, result, shouldCancel))
    {
        return false;
    }
    result.sourceOverlapRatio = static_cast<double>(
        result.overlappingSourcePointCount) / static_cast<double>(alignedSource->size());
    result.targetOverlapRatio = static_cast<double>(
        result.overlappingTargetPointCount) / static_cast<double>(target->size());
    result.overlapRatio = std::min(
        result.sourceOverlapRatio, result.targetOverlapRatio);
    return true;
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
    if (!std::isfinite(options.minimumOverlapRatio)
        || options.minimumOverlapRatio < 0.01
        || options.minimumOverlapRatio > 1.0)
    {
        setError(errorMessage, QStringLiteral("最小双向重叠率必须在 1% 到 100% 之间。"));
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

    auto finiteSource = finiteCopy(
        source, result, shouldCancel, &result.sourceIndices);
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
        result.combinedTransform = result.transform.cast<double>()
            * result.sourceToTargetFrame;
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
        auto alignedRegistrationSource =
            pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
                new pcl::PointCloud<pcl::PointXYZRGB>(preview));
        if (!calculateBidirectionalOverlap(
                alignedRegistrationSource, registrationTarget,
                options.maximumCorrespondenceDistance, result, shouldCancel))
        {
            return result;
        }
        if (result.overlapRatio < options.minimumOverlapRatio)
        {
            result.converged = false;
            result.error = QStringLiteral(
                "ICP 双向有效重叠率仅 %1%（源 %2%，目标 %3%），低于要求的 %4%。"
                "请改善初始位置、调整最大对应距离，或确认确属低重叠数据后降低门槛。")
                .arg(result.overlapRatio * 100.0, 0, 'f', 1)
                .arg(result.sourceOverlapRatio * 100.0, 0, 'f', 1)
                .arg(result.targetOverlapRatio * 100.0, 0, 'f', 1)
                .arg(options.minimumOverlapRatio * 100.0, 0, 'f', 1);
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

Result alignInTargetFrame(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
    const famp::cloud::SpatialReference& sourceSpatial,
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& target,
    const famp::cloud::SpatialReference& targetSpatial,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    Result failed;
    failed.sourcePointCount = source ? source->size() : 0;
    failed.targetPointCount = target ? target->size() : 0;
    if (!source || !target)
    {
        failed.error = QStringLiteral("源点云或目标点云为空指针。");
        return failed;
    }
    if (!sourceToTargetMatrix(
            sourceSpatial, targetSpatial, failed.sourceToTargetFrame))
    {
        failed.error = QStringLiteral("点云局部坐标系无效或不可逆。");
        return failed;
    }
    if (cancel(failed, shouldCancel))
        return failed;

    auto converted = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    converted->resize(source->size());
    converted->width = source->width;
    converted->height = source->height;
    converted->is_dense = source->is_dense;
    for (std::size_t index = 0; index < source->size(); ++index)
    {
        if ((index & 0x0fffU) == 0U && cancel(failed, shouldCancel))
            return failed;
        const pcl::PointXYZRGB& point = source->points[index];
        pcl::PointXYZRGB transformed = point;
        if (pcl::isFinite(point))
        {
            const Eigen::Vector4d value = failed.sourceToTargetFrame
                * Eigen::Vector4d(point.x, point.y, point.z, 1.0);
            if (!value.allFinite() || std::abs(value.w()) < 1.0e-12)
            {
                failed.error = QStringLiteral("点云局部坐标转换结果无效。");
                return failed;
            }
            const double x = value.x() / value.w();
            const double y = value.y() / value.w();
            const double z = value.z() / value.w();
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
                || std::abs(x) > std::numeric_limits<float>::max()
                || std::abs(y) > std::numeric_limits<float>::max()
                || std::abs(z) > std::numeric_limits<float>::max())
            {
                failed.error = QStringLiteral("转换后的目标局部坐标超出安全范围。");
                return failed;
            }
            transformed.x = static_cast<float>(x);
            transformed.y = static_cast<float>(y);
            transformed.z = static_cast<float>(z);
        }
        converted->points[index] = transformed;
    }

    Result result = align(converted, target, options, shouldCancel);
    result.sourceToTargetFrame = failed.sourceToTargetFrame;
    result.combinedTransform = result.transform.cast<double>()
        * result.sourceToTargetFrame;
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
