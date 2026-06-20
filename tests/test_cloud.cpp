#include <gtest/gtest.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/common.h>

#include "Cloud.h"

// ── Helper: build a cloud from a list of (x, y, z) triples ──────────────
static pcl::PointCloud<pcl::PointXYZRGB>::Ptr
makeCloud(const std::vector<pcl::PointXYZ> &pts)
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
    cloud->reserve(pts.size());
    for (const auto &p : pts) {
        pcl::PointXYZRGB pt;
        pt.x = p.x;
        pt.y = p.y;
        pt.z = p.z;
        pt.r = 255;
        pt.g = 255;
        pt.b = 255;
        cloud->push_back(pt);
    }
    return cloud;
}

// ── Centroid tests ──────────────────────────────────────────────────────

TEST(CloudTest, CentroidOfUnitCube)
{
    // Eight corners of the unit cube [0,1]³
    auto cloud = makeCloud({
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
    });

    Cloud c(cloud);

    EXPECT_NEAR(c.centriod[0], 0.5f, 1e-6f);
    EXPECT_NEAR(c.centriod[1], 0.5f, 1e-6f);
    EXPECT_NEAR(c.centriod[2], 0.5f, 1e-6f);
}

TEST(CloudTest, CentroidOfShiftedCube)
{
    // Cube centered at (5, 0, -3) with side length 2
    auto cloud = makeCloud({
        {4.0f, -1.0f, -4.0f}, {6.0f, -1.0f, -4.0f},
        {4.0f,  1.0f, -4.0f}, {4.0f, -1.0f, -2.0f},
        {6.0f,  1.0f, -4.0f}, {6.0f, -1.0f, -2.0f},
        {4.0f,  1.0f, -2.0f}, {6.0f,  1.0f, -2.0f},
    });

    Cloud c(cloud);

    EXPECT_NEAR(c.centriod[0], 5.0f, 1e-6f);
    EXPECT_NEAR(c.centriod[1], 0.0f, 1e-6f);
    EXPECT_NEAR(c.centriod[2], -3.0f, 1e-6f);
}

TEST(CloudTest, CentroidOfSinglePoint)
{
    auto cloud = makeCloud({{42.0f, -7.0f, 13.0f}});
    Cloud c(cloud);

    EXPECT_NEAR(c.centriod[0], 42.0f, 1e-6f);
    EXPECT_NEAR(c.centriod[1], -7.0f, 1e-6f);
    EXPECT_NEAR(c.centriod[2], 13.0f, 1e-6f);
}

// ── OBB tests ───────────────────────────────────────────────────────────

TEST(CloudTest, OBBReturnsNonDegenerateBox)
{
    auto cloud = makeCloud({
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
    });

    Cloud c(cloud);

    pcl::PointXYZRGB min_obb, max_obb, pos_obb;
    c.computeOBB(min_obb, max_obb, pos_obb);

    // The OBB should span a non-zero extent in each dimension
    float dx = max_obb.x - min_obb.x;
    float dy = max_obb.y - min_obb.y;
    float dz = max_obb.z - min_obb.z;

    EXPECT_GT(dx, 0.0f);
    EXPECT_GT(dy, 0.0f);
    EXPECT_GT(dz, 0.0f);
}

TEST(CloudTest, OBBContainsOriginalPoints)
{
    // A simple 2×2×2 cube around the origin
    auto cloud = makeCloud({
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f, -1.0f}, { 1.0f, -1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f},
    });

    Cloud c(cloud);

    pcl::PointXYZRGB min_obb, max_obb, pos_obb;
    c.computeOBB(min_obb, max_obb, pos_obb);

    // pos_obb should be near the centroid of the cloud (0, 0, 0)
    EXPECT_NEAR(pos_obb.x, 0.0f, 0.1f);
    EXPECT_NEAR(pos_obb.y, 0.0f, 0.1f);
    EXPECT_NEAR(pos_obb.z, 0.0f, 0.1f);
}

// ── Decentering tests ───────────────────────────────────────────────────

TEST(CloudTest, DecenteredCloudCentroidAtOrigin)
{
    auto cloud = makeCloud({
        {10.0f, 20.0f, 30.0f},
        {12.0f, 20.0f, 30.0f},
        {10.0f, 22.0f, 30.0f},
        {10.0f, 20.0f, 32.0f},
    });
    // Centroid: (10.5, 20.5, 30.5)

    Cloud c(cloud);
    auto decentered = c.computeDecentrationCloud();

    Eigen::Vector4f decentered_centroid;
    pcl::compute3DCentroid(*decentered, decentered_centroid);

    EXPECT_NEAR(decentered_centroid[0], 0.0f, 1e-6f);
    EXPECT_NEAR(decentered_centroid[1], 0.0f, 1e-6f);
    EXPECT_NEAR(decentered_centroid[2], 0.0f, 1e-6f);
}

TEST(CloudTest, DecenteringPreservesPointCount)
{
    auto cloud = makeCloud({
        {1.0f, 2.0f, 3.0f},
        {4.0f, 5.0f, 6.0f},
        {7.0f, 8.0f, 9.0f},
    });

    Cloud c(cloud);
    auto decentered = c.computeDecentrationCloud();

    EXPECT_EQ(decentered->size(), cloud->size());
}

TEST(CloudTest, DecenteringPreservesRelativePositions)
{
    // Two points with a known offset
    auto cloud = makeCloud({
        {5.0f, 5.0f, 5.0f},
        {7.0f, 9.0f, 11.0f},
    });

    Cloud c(cloud);  // centroid = (6, 7, 8)
    auto decentered = c.computeDecentrationCloud();

    // After decentering, point 0 ≈ (-1, -2, -3), point 1 ≈ (1, 2, 3)
    EXPECT_NEAR(decentered->at(0).x, -1.0f, 1e-6f);
    EXPECT_NEAR(decentered->at(0).y, -2.0f, 1e-6f);
    EXPECT_NEAR(decentered->at(0).z, -3.0f, 1e-6f);

    EXPECT_NEAR(decentered->at(1).x,  1.0f, 1e-6f);
    EXPECT_NEAR(decentered->at(1).y,  2.0f, 1e-6f);
    EXPECT_NEAR(decentered->at(1).z,  3.0f, 1e-6f);

    // Colors should be preserved
    EXPECT_EQ(decentered->at(0).r, cloud->at(0).r);
    EXPECT_EQ(decentered->at(0).g, cloud->at(0).g);
    EXPECT_EQ(decentered->at(0).b, cloud->at(0).b);
}
