#include "PlaneWidgetPlacement.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{

double distance(const std::array<double, 3>& left,
                const std::array<double, 3>& right)
{
    double squared = 0.0;
    for (std::size_t axis = 0; axis < left.size(); ++axis)
    {
        const double delta = left[axis] - right[axis];
        squared += delta * delta;
    }
    return std::sqrt(squared);
}

pcl::PointCloud<pcl::PointXYZRGB> boxCloud(
    float halfX, float halfY, float halfZ)
{
    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    for (const float x : {-halfX, halfX})
    {
        for (const float y : {-halfY, halfY})
        {
            for (const float z : {-halfZ, halfZ})
                cloud.push_back(pcl::PointXYZRGB(x, y, z));
        }
    }
    return cloud;
}

} // namespace

TEST(PlaneWidgetPlacementTest, FitsAnisotropicCloudWithSmallMargin)
{
    const auto placement = famp::viewport::fitPlaneWidgetToCloud(
        boxCloud(10.0f, 2.0f, 1.0f),
        {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    ASSERT_TRUE(placement.has_value());

    EXPECT_NEAR(distance(placement->origin, placement->point1), 21.0, 1e-9);
    EXPECT_NEAR(distance(placement->origin, placement->point2), 4.2, 1e-9);
    EXPECT_NEAR(placement->handleSize, 0.006, 1e-12);
    EXPECT_NEAR(placement->normalHandleLengthRatio, 0.12, 1e-12);
    for (std::size_t axis = 0; axis < placement->origin.size(); ++axis)
    {
        const double center = placement->origin[axis]
            + (placement->point1[axis] - placement->origin[axis]) * 0.5
            + (placement->point2[axis] - placement->origin[axis]) * 0.5;
        EXPECT_NEAR(center, 0.0, 1e-9);
    }
}

TEST(PlaneWidgetPlacementTest, KeepsFlatCloudPlaneLargeEnoughToManipulate)
{
    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    cloud.push_back(pcl::PointXYZRGB(-10.0f, 0.0f, 0.0f));
    cloud.push_back(pcl::PointXYZRGB(10.0f, 0.0f, 0.0f));

    const auto placement = famp::viewport::fitPlaneWidgetToCloud(
        cloud, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0});
    ASSERT_TRUE(placement.has_value());
    EXPECT_NEAR(distance(placement->origin, placement->point1), 21.0, 1e-9);
    EXPECT_NEAR(distance(placement->origin, placement->point2), 1.05, 1e-9);
    EXPECT_NEAR(placement->handleSize, 0.002, 1e-12);
    EXPECT_NEAR(placement->normalHandleLengthRatio, 0.05, 1e-12);
}

TEST(PlaneWidgetPlacementTest, RejectsEmptyCloudAndInvalidNormal)
{
    pcl::PointCloud<pcl::PointXYZRGB> empty;
    EXPECT_FALSE(famp::viewport::fitPlaneWidgetToCloud(
        empty, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}).has_value());

    const auto cloud = boxCloud(1.0f, 1.0f, 1.0f);
    EXPECT_FALSE(famp::viewport::fitPlaneWidgetToCloud(
        cloud, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}).has_value());
}
