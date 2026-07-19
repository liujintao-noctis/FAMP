#include "CloudCoordinates.h"

#include <Eigen/Dense>

#include <cmath>

namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool finitePoint(const famp::cloud::Point3d& point)
{
    return std::isfinite(point[0])
        && std::isfinite(point[1])
        && std::isfinite(point[2]);
}

bool matrix(const famp::cloud::SpatialReference& spatial,
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
    return finitePoint(spatial.origin);
}

bool homogeneousPoint(const Eigen::Vector4d& value,
                      famp::cloud::Point3d& result)
{
    if (!value.allFinite() || std::abs(value.w()) < 1.0e-12)
        return false;
    result = {value.x() / value.w(),
              value.y() / value.w(),
              value.z() / value.w()};
    return finitePoint(result);
}
}

namespace famp::cloud
{
bool localToReal(const SpatialReference& spatial,
                 const Point3d& local,
                 Point3d& real,
                 QString* errorMessage)
{
    Eigen::Matrix4d transform;
    if (!finitePoint(local) || !matrix(spatial, transform))
    {
        setError(errorMessage, QStringLiteral("点云坐标或变换包含非有限值。"));
        return false;
    }
    const Eigen::Vector4d source(
        local[0] + spatial.origin[0],
        local[1] + spatial.origin[1],
        local[2] + spatial.origin[2],
        1.0);
    Point3d candidate;
    if (!homogeneousPoint(transform * source, candidate))
    {
        setError(errorMessage, QStringLiteral("点云坐标变换结果无效。"));
        return false;
    }
    real = candidate;
    return true;
}

bool realToLocal(const SpatialReference& spatial,
                 const Point3d& real,
                 Point3d& local,
                 QString* errorMessage)
{
    Eigen::Matrix4d transform;
    if (!finitePoint(real) || !matrix(spatial, transform))
    {
        setError(errorMessage, QStringLiteral("点云坐标或变换包含非有限值。"));
        return false;
    }
    const Eigen::FullPivLU<Eigen::Matrix4d> decomposition(transform);
    if (!decomposition.isInvertible())
    {
        setError(errorMessage, QStringLiteral("点云变换矩阵不可逆。"));
        return false;
    }
    Point3d source;
    if (!homogeneousPoint(
            decomposition.inverse()
                * Eigen::Vector4d(real[0], real[1], real[2], 1.0),
            source))
    {
        setError(errorMessage, QStringLiteral("点云逆变换结果无效。"));
        return false;
    }
    Point3d candidate{
        source[0] - spatial.origin[0],
        source[1] - spatial.origin[1],
        source[2] - spatial.origin[2]};
    if (!finitePoint(candidate))
    {
        setError(errorMessage, QStringLiteral("点云局部坐标结果无效。"));
        return false;
    }
    local = candidate;
    return true;
}
}
