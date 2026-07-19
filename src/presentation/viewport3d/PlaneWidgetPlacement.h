#pragma once

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <array>
#include <optional>

namespace famp::viewport
{

struct PlaneWidgetPlacement
{
    std::array<double, 3> origin{};
    std::array<double, 3> point1{};
    std::array<double, 3> point2{};
    // vtkPlaneWidget interprets this as a fraction of its fitted diagonal.
    // Keeping it with the placement makes handle/cone sizing follow the
    // selected cloud instead of vtkPlaneWidget's visually dominant default.
    double handleSize = 0.006;
    // Half-length of each normal/rotation arrow as a fitted-diagonal ratio.
    // vtkPlaneWidget hard-codes 0.35, which is too prominent for this UI.
    double normalHandleLengthRatio = 0.12;
};

// Fits a finite plane to the projection of the cloud bounds. The preferred
// axis controls the plane's initial in-plane orientation.
std::optional<PlaneWidgetPlacement> fitPlaneWidgetToCloud(
    const pcl::PointCloud<pcl::PointXYZRGB>& cloud,
    const std::array<double, 3>& normal,
    const std::array<double, 3>& preferredAxis,
    double marginRatio = 0.05);

} // namespace famp::viewport
