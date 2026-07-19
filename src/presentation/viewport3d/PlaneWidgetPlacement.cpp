#include "PlaneWidgetPlacement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

using Vector3d = std::array<double, 3>;

double dot(const Vector3d& left, const Vector3d& right)
{
    return left[0] * right[0]
        + left[1] * right[1]
        + left[2] * right[2];
}

Vector3d cross(const Vector3d& left, const Vector3d& right)
{
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]};
}

bool normalize(Vector3d& vector)
{
    const double length = std::sqrt(dot(vector, vector));
    if (!std::isfinite(length)
        || length <= std::numeric_limits<double>::epsilon())
    {
        return false;
    }
    for (double& component : vector)
        component /= length;
    return true;
}

Vector3d projectedAxis(const Vector3d& axis, const Vector3d& normal)
{
    const double normalComponent = dot(axis, normal);
    return {
        axis[0] - normalComponent * normal[0],
        axis[1] - normalComponent * normal[1],
        axis[2] - normalComponent * normal[2]};
}

Vector3d fallbackAxis(const Vector3d& normal)
{
    int index = 0;
    if (std::abs(normal[1]) < std::abs(normal[index]))
        index = 1;
    if (std::abs(normal[2]) < std::abs(normal[index]))
        index = 2;
    Vector3d axis{};
    axis[static_cast<std::size_t>(index)] = 1.0;
    return axis;
}

Vector3d offset(const Vector3d& center,
                const Vector3d& firstAxis,
                double firstDistance,
                const Vector3d& secondAxis,
                double secondDistance)
{
    return {
        center[0] + firstAxis[0] * firstDistance
            + secondAxis[0] * secondDistance,
        center[1] + firstAxis[1] * firstDistance
            + secondAxis[1] * secondDistance,
        center[2] + firstAxis[2] * firstDistance
            + secondAxis[2] * secondDistance};
}

double adaptiveHandleSize(double firstLength, double secondLength)
{
    const double diagonal = std::hypot(firstLength, secondLength);
    if (!std::isfinite(diagonal)
        || diagonal <= std::numeric_limits<double>::epsilon())
    {
        return 0.006;
    }

    // vtkPlaneWidget creates each sphere/cone radius as approximately
    // 1.25 * HandleSize * fittedDiagonal before the first pick. Aim for a
    // subtle 0.75% diagonal radius, but reduce it on long, narrow planes so
    // the handles never dominate the short edge.
    const double shortEdge = std::min(firstLength, secondLength);
    const double desiredRadius = std::min(
        diagonal * 0.0075,
        std::max(shortEdge * 0.04, diagonal * 0.0025));
    return std::clamp(
        desiredRadius / (1.25 * diagonal), 0.002, 0.006);
}

double adaptiveNormalLengthRatio(double firstLength, double secondLength)
{
    const double diagonal = std::hypot(firstLength, secondLength);
    if (!std::isfinite(diagonal)
        || diagonal <= std::numeric_limits<double>::epsilon())
    {
        return 0.12;
    }
    const double shortEdge = std::min(firstLength, secondLength);
    const double desiredLength = std::max(
        diagonal * 0.05,
        std::min(diagonal * 0.12, shortEdge * 0.8));
    return std::clamp(desiredLength / diagonal, 0.05, 0.12);
}

} // namespace

namespace famp::viewport
{

std::optional<PlaneWidgetPlacement> fitPlaneWidgetToCloud(
    const pcl::PointCloud<pcl::PointXYZRGB>& cloud,
    const std::array<double, 3>& requestedNormal,
    const std::array<double, 3>& preferredAxis,
    double marginRatio)
{
    if (cloud.empty() || !std::isfinite(marginRatio)
        || marginRatio < 0.0 || marginRatio > 1.0)
    {
        return std::nullopt;
    }

    Vector3d normal = requestedNormal;
    if (!normalize(normal))
        return std::nullopt;

    Vector3d firstAxis = projectedAxis(preferredAxis, normal);
    if (!normalize(firstAxis))
    {
        firstAxis = projectedAxis(fallbackAxis(normal), normal);
        if (!normalize(firstAxis))
            return std::nullopt;
    }
    Vector3d secondAxis = cross(normal, firstAxis);
    if (!normalize(secondAxis))
        return std::nullopt;

    Vector3d minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    Vector3d maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    bool hasFinitePoint = false;
    for (const pcl::PointXYZRGB& point : cloud.points)
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)
            || !std::isfinite(point.z))
        {
            continue;
        }
        const Vector3d value{point.x, point.y, point.z};
        for (std::size_t axis = 0; axis < value.size(); ++axis)
        {
            minimum[axis] = std::min(minimum[axis], value[axis]);
            maximum[axis] = std::max(maximum[axis], value[axis]);
        }
        hasFinitePoint = true;
    }
    if (!hasFinitePoint)
        return std::nullopt;

    Vector3d center{};
    Vector3d halfBounds{};
    double coordinateScale = 1.0;
    for (std::size_t axis = 0; axis < center.size(); ++axis)
    {
        center[axis] = (minimum[axis] + maximum[axis]) * 0.5;
        halfBounds[axis] = (maximum[axis] - minimum[axis]) * 0.5;
        coordinateScale = std::max(
            coordinateScale,
            std::max(std::abs(minimum[axis]), std::abs(maximum[axis])));
    }

    double firstHalfExtent = 0.0;
    double secondHalfExtent = 0.0;
    for (std::size_t axis = 0; axis < halfBounds.size(); ++axis)
    {
        firstHalfExtent += std::abs(firstAxis[axis]) * halfBounds[axis];
        secondHalfExtent += std::abs(secondAxis[axis]) * halfBounds[axis];
    }

    double projectedReference = std::max(firstHalfExtent, secondHalfExtent);
    if (projectedReference <= std::numeric_limits<double>::epsilon())
    {
        projectedReference = std::max(
            {halfBounds[0], halfBounds[1], halfBounds[2], 0.5});
    }
    const double numericHalfExtent = coordinateScale
        * static_cast<double>(std::numeric_limits<float>::epsilon()) * 16.0;
    const double minimumHalfExtent = std::max(
        projectedReference * 0.05, numericHalfExtent);
    const double marginFactor = 1.0 + marginRatio;
    firstHalfExtent = std::max(firstHalfExtent, minimumHalfExtent)
        * marginFactor;
    secondHalfExtent = std::max(secondHalfExtent, minimumHalfExtent)
        * marginFactor;

    PlaneWidgetPlacement placement;
    placement.origin = offset(
        center, firstAxis, -firstHalfExtent,
        secondAxis, -secondHalfExtent);
    placement.point1 = offset(
        center, firstAxis, firstHalfExtent,
        secondAxis, -secondHalfExtent);
    placement.point2 = offset(
        center, firstAxis, -firstHalfExtent,
        secondAxis, secondHalfExtent);
    placement.handleSize = adaptiveHandleSize(
        firstHalfExtent * 2.0, secondHalfExtent * 2.0);
    placement.normalHandleLengthRatio = adaptiveNormalLengthRatio(
        firstHalfExtent * 2.0, secondHalfExtent * 2.0);
    return placement;
}

} // namespace famp::viewport
