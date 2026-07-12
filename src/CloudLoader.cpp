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

LoadResult load(const QString& requestedPath)
{
    LoadResult result;
    if (!validatePath(requestedPath, &result.path, &result.error))
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
            if (loadedPoints->empty())
            {
                result.error = QStringLiteral("点云不包含可用点：\n%1")
                                   .arg(result.path);
                return result;
            }

            Cloud cloud(loadedPoints);
            result.sourceCloud = loadedPoints;
            result.displayCloud = cloud.computeDecentrationCloud();
            result.sourceWasPcd = true;
        }
        else
        {
            if (!loadLasAsRgb(result.path, loadedPoints, &loadError))
            {
                result.error = loadError;
                return result;
            }
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
