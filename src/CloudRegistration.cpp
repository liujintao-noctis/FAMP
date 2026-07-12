#include "CloudRegistration.h"

#include "FileIO.h"

#include <pcl/common/point_tests.h>
#include <pcl/common/transforms.h>
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
        pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> icp;
        icp.setInputSource(finiteSource);
        icp.setInputTarget(finiteTarget);
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
        if (!result.converged || !std::isfinite(result.fitnessScore))
        {
            result.converged = false;
            result.error = QStringLiteral("ICP 未收敛，请检查初始位置或增大最大对应距离。");
            return result;
        }
        result.transform = icp.getFinalTransformation();
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
    const famp::tasks::CancellationCheck& shouldCancel)
{
    Result result = align(source, target, options, shouldCancel);
    if (!result.succeeded() || cancel(result, shouldCancel))
        return result;
    result.outputPath = famp::io::pathWithRequiredSuffix(
        requestedOutputPath, QStringLiteral("pcd"));
    if (!famp::io::savePcdAsciiAtomically(
            result.outputPath, *result.cloud, &result.error))
    {
        result.cloud.reset();
        result.outputPointCount = 0;
        result.converged = false;
    }
    return result;
}
}
