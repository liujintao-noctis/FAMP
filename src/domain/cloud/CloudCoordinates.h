#pragma once

#include <array>

#include <QString>

namespace famp::cloud
{
struct SpatialReference
{
    std::array<double, 3> origin{0.0, 0.0, 0.0};
    std::array<double, 16> transform{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0};
};

using Point3d = std::array<double, 3>;

bool localToReal(const SpatialReference& spatial,
                 const Point3d& local,
                 Point3d& real,
                 QString* errorMessage = nullptr);

bool realToLocal(const SpatialReference& spatial,
                 const Point3d& real,
                 Point3d& local,
                 QString* errorMessage = nullptr);
}
