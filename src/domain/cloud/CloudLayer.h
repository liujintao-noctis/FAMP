#pragma once

#include "CloudAttributes.h"
#include "CloudCoordinates.h"
#include "CloudDisplaySettings.h"
#include "ControlPoints.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QMap>
#include <QString>

namespace famp::cloud
{
struct CloudLayer
{
    QString id;
    QString name;
    QString sourcePath;
    QString crs;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr points;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr sourcePoints;
    SpatialReference spatial;
    CloudAttributes attributes;
    famp::display::Settings display;
    QMap<QString, QString> archaeologyFields;
    QVector<famp::control::Point> controlPoints;
    bool visible = true;
    bool locked = false;
    quint64 revision = 0;

    std::size_t pointCount() const;
    bool isEmpty() const;
};

QString createLayerId();
QString stableLayerId(const QString& seed);
bool isValidLayerId(const QString& id);

CloudLayer makeLayer(
    const QString& sourcePath,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
    const SpatialReference& spatial = {},
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& sourcePoints = {});

bool validateLayer(const CloudLayer& layer,
                   bool requirePoints = true,
                   QString* errorMessage = nullptr);
}
