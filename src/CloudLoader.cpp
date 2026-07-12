/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Description: Thread-safe point-cloud loading service
 *****************************************************************/

#include "CloudLoader.h"

#include "Cloud.h"
#include "LasLoader.h"
#include "PcdLoader.h"
#include "RecentFiles.h"

#include <QFileInfo>

#include <exception>

namespace famp::cloud
{
namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool cancel(LoadResult& result,
            const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!famp::tasks::isCancellationRequested(shouldCancel))
        return false;

    result.displayCloud.reset();
    result.sourceCloud.reset();
    result.cancelled = true;
    result.error = QStringLiteral("点云加载已取消。");
    return true;
}
}

bool validatePath(const QString& requestedPath,
                  QString* normalizedPath,
                  QString* errorMessage)
{
    const QString path = famp::recent::normalizedPath(requestedPath);
    if (normalizedPath)
        *normalizedPath = path;

    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        setError(errorMessage, QStringLiteral("文件不存在：\n%1").arg(path));
        return false;
    }
    if (!fileInfo.isReadable())
    {
        setError(errorMessage, QStringLiteral("文件不可读：\n%1").arg(path));
        return false;
    }
    if (!famp::recent::isSupportedCloudFile(path))
    {
        setError(errorMessage,
                 QStringLiteral("仅支持 PCD 和 LAS 点云文件：\n%1").arg(path));
        return false;
    }
    return true;
}

LoadResult load(const QString& requestedPath,
                const famp::tasks::CancellationCheck& shouldCancel)
{
    LoadResult result;
    if (!validatePath(requestedPath, &result.path, &result.error))
        return result;
    if (cancel(result, shouldCancel))
        return result;

    try
    {
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr loadedPoints(
            new pcl::PointCloud<pcl::PointXYZRGB>);
        const QString suffix = QFileInfo(result.path).suffix().toLower();
        QString loadError;

        if (suffix == QStringLiteral("pcd"))
        {
            if (!loadPcdAsRgb(result.path, loadedPoints, &loadError))
            {
                result.error = loadError;
                return result;
            }
            if (cancel(result, shouldCancel))
                return result;
            if (loadedPoints->empty())
            {
                result.error = QStringLiteral("点云不包含可用点：\n%1")
                                   .arg(result.path);
                return result;
            }

            result.sourceCloud = loadedPoints;
            long double sumX = 0.0;
            long double sumY = 0.0;
            long double sumZ = 0.0;
            std::size_t pointIndex = 0;
            for (const pcl::PointXYZRGB& point : loadedPoints->points)
            {
                if ((pointIndex++ & 0x0fffU) == 0U
                    && cancel(result, shouldCancel))
                {
                    return result;
                }
                sumX += point.x;
                sumY += point.y;
                sumZ += point.z;
            }
            const long double count = static_cast<long double>(
                loadedPoints->size());
            result.spatial.origin = {
                static_cast<double>(sumX / count),
                static_cast<double>(sumY / count),
                static_cast<double>(sumZ / count)};
            result.displayCloud.reset(
                new pcl::PointCloud<pcl::PointXYZRGB>(*loadedPoints));
            pointIndex = 0;
            for (pcl::PointXYZRGB& point : result.displayCloud->points)
            {
                if ((pointIndex++ & 0x0fffU) == 0U
                    && cancel(result, shouldCancel))
                {
                    return result;
                }
                point.x = static_cast<float>(
                    static_cast<double>(point.x) - result.spatial.origin[0]);
                point.y = static_cast<float>(
                    static_cast<double>(point.y) - result.spatial.origin[1]);
                point.z = static_cast<float>(
                    static_cast<double>(point.z) - result.spatial.origin[2]);
            }
            result.sourceWasPcd = true;
        }
        else
        {
            if (!loadLasAsRgb(
                    result.path, loadedPoints, &loadError,
                    &result.spatial.origin))
            {
                result.error = loadError;
                return result;
            }
            if (cancel(result, shouldCancel))
                return result;
            result.displayCloud = loadedPoints;
        }

        if (!result.displayCloud || result.displayCloud->empty())
        {
            result.displayCloud.reset();
            result.error = QStringLiteral("点云不包含可用点：\n%1")
                               .arg(result.path);
        }
    }
    catch (const std::exception& exception)
    {
        result.displayCloud.reset();
        result.error = QStringLiteral("读取点云时发生错误：%1\n%2")
                           .arg(QString::fromLocal8Bit(exception.what()), result.path);
    }
    catch (...)
    {
        result.displayCloud.reset();
        result.error = QStringLiteral("读取点云时发生未知错误：\n%1")
                           .arg(result.path);
    }
    return result;
}
}
