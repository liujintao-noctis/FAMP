#include <gtest/gtest.h>

#include "CloudCoordinates.h"
#include "CloudReprojection.h"
#include "CrsService.h"
#include "FileIO.h"
#include "PcdLoader.h"

#include <QDir>
#include <QTemporaryDir>

#include <atomic>
#include <limits>
#include <utility>
#include <vector>

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr geographicCloud()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    cloud->resize(2);
    cloud->width = 2;
    cloud->height = 1;
    cloud->points[0].x = 0.0F;
    cloud->points[0].y = 0.0F;
    cloud->points[0].z = 0.0F;
    cloud->points[0].r = 10;
    cloud->points[0].g = 20;
    cloud->points[0].b = 30;
    cloud->points[1].x = 0.001F;
    cloud->points[1].y = 0.0F;
    cloud->points[1].z = 2.0F;
    cloud->points[1].r = 40;
    cloud->points[1].g = 50;
    cloud->points[1].b = 60;
    return cloud;
}
}

TEST(CloudReprojectionTest, ReprojectsAndRecentersWithoutLosingColor)
{
    famp::cloud::SpatialReference sourceSpatial;
    sourceSpatial.origin = {12.0, 55.0, 7.0};
    std::vector<double> progress;
    const auto result = famp::cloud::reproject(
        geographicCloud(), sourceSpatial,
        QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"), {},
        [&progress](double value) { progress.push_back(value); });

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    ASSERT_EQ(result.points->size(), 2U);
    EXPECT_EQ(result.sourceCrs, QStringLiteral("EPSG:4326"));
    EXPECT_EQ(result.targetCrs, QStringLiteral("EPSG:3857"));
    EXPECT_NEAR(result.points->points[0].x, 0.0, 1.0e-5);
    EXPECT_NEAR(result.points->points[0].y, 0.0, 1.0e-5);
    EXPECT_NEAR(result.points->points[0].z, 0.0, 1.0e-5);
    EXPECT_NEAR(result.points->points[1].x, 111.3195, 0.02);
    EXPECT_NEAR(result.points->points[1].y, 0.0, 0.02);
    EXPECT_NEAR(result.points->points[1].z, 2.0, 1.0e-4);
    EXPECT_EQ(result.points->points[1].r, 40);
    EXPECT_EQ(result.points->points[1].g, 50);
    EXPECT_EQ(result.points->points[1].b, 60);
    ASSERT_FALSE(progress.empty());
    EXPECT_DOUBLE_EQ(progress.back(), 1.0);

    famp::cloud::Point3d restoredReal;
    QString error;
    ASSERT_TRUE(famp::cloud::localToReal(
        result.spatial,
        {result.points->points[1].x,
         result.points->points[1].y,
         result.points->points[1].z},
        restoredReal,
        &error)) << error.toStdString();
    famp::crs::Coordinate expected;
    ASSERT_TRUE(famp::crs::transform(
        QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"),
        {12.001, 55.0, 9.0}, expected, &error)) << error.toStdString();
    EXPECT_NEAR(restoredReal[0], expected.x, 0.02);
    EXPECT_NEAR(restoredReal[1], expected.y, 0.02);
    EXPECT_NEAR(restoredReal[2], expected.z, 1.0e-4);
}

TEST(CloudReprojectionTest, AppliesExistingAffineSpatialTransform)
{
    auto cloud = geographicCloud();
    cloud->resize(1);
    cloud->width = 1;
    famp::cloud::SpatialReference spatial;
    spatial.origin = {11.0, 54.0, 7.0};
    spatial.transform = {
        1.0, 0.0, 0.0, 1.0,
        0.0, 1.0, 0.0, 1.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0};

    const auto result = famp::cloud::reproject(
        cloud, spatial, QStringLiteral("4326"), QStringLiteral("3857"));
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();

    famp::crs::Coordinate expected;
    QString error;
    ASSERT_TRUE(famp::crs::transform(
        QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"),
        {12.0, 55.0, 7.0}, expected, &error)) << error.toStdString();
    EXPECT_NEAR(result.spatial.origin[0], expected.x, 0.01);
    EXPECT_NEAR(result.spatial.origin[1], expected.y, 0.01);
    EXPECT_NEAR(result.spatial.origin[2], expected.z, 1.0e-9);
}

TEST(CloudReprojectionTest, CancelsAtomicallyAndRejectsInvalidInput)
{
    const auto input = geographicCloud();
    famp::cloud::SpatialReference spatial;
    spatial.origin = {12.0, 55.0, 0.0};
    auto cancelled = std::make_shared<std::atomic_bool>(true);
    auto result = famp::cloud::reproject(
        input, spatial, QStringLiteral("EPSG:4326"),
        QStringLiteral("EPSG:3857"),
        [cancelled]() { return cancelled->load(); });
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(result.points);
    EXPECT_FALSE(result.error.isEmpty());

    result = famp::cloud::reproject(
        input, spatial, QStringLiteral("EPSG:4326"),
        QStringLiteral("EPSG:4326"));
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("相同")));

    input->points[0].x = std::numeric_limits<float>::quiet_NaN();
    result = famp::cloud::reproject(
        input, spatial, QStringLiteral("EPSG:4326"),
        QStringLiteral("EPSG:3857"));
    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.points);
    EXPECT_TRUE(result.error.contains(QStringLiteral("第 1 个点")));
}

TEST(CloudReprojectionTest, PersistsLocalPrecisionAndSpatialOriginInPcd)
{
    famp::cloud::SpatialReference sourceSpatial;
    sourceSpatial.origin = {12.0, 55.0, 7.0};
    const auto projected = famp::cloud::reproject(
        geographicCloud(), sourceSpatial,
        QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"));
    ASSERT_TRUE(projected.succeeded()) << projected.error.toStdString();

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = QDir(directory.path()).filePath(
        QStringLiteral("重投影结果.pcd"));
    famp::cloud::CloudAttributes attributes;
    famp::cloud::AttributeChannel classifications;
    classifications.name = QStringLiteral("classification");
    classifications.type = famp::cloud::AttributeValueType::UnsignedInteger;
    classifications.unsignedValues = {2, 7};
    QString error;
    ASSERT_TRUE(attributes.insert(std::move(classifications), 2, &error))
        << error.toStdString();
    ASSERT_TRUE(famp::io::savePcdAsciiAtomically(
        path, *projected.points, &error, &projected.spatial, &attributes))
        << error.toStdString();

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    famp::cloud::SpatialReference loadedSpatial;
    bool hasEmbeddedSpatial = false;
    famp::cloud::CloudAttributes loadedAttributes;
    ASSERT_TRUE(loadPcdAsRgb(
        path, loaded, &error, &loadedSpatial, &hasEmbeddedSpatial,
        &loadedAttributes))
        << error.toStdString();
    ASSERT_TRUE(hasEmbeddedSpatial);
    ASSERT_EQ(loaded->size(), projected.points->size());
    EXPECT_NEAR(loadedSpatial.origin[0], projected.spatial.origin[0], 1.0e-9);
    EXPECT_NEAR(loadedSpatial.origin[1], projected.spatial.origin[1], 1.0e-9);
    EXPECT_NEAR(loaded->points[1].x, projected.points->points[1].x, 1.0e-6);
    EXPECT_EQ(loaded->points[1].r, projected.points->points[1].r);
    EXPECT_EQ(loaded->points[1].g, projected.points->points[1].g);
    EXPECT_EQ(loaded->points[1].b, projected.points->points[1].b);
    const auto* loadedClassifications = loadedAttributes.channel(
        QStringLiteral("classification"));
    ASSERT_NE(loadedClassifications, nullptr);
    EXPECT_EQ(loadedClassifications->type,
              famp::cloud::AttributeValueType::UnsignedInteger);
    EXPECT_EQ(loadedClassifications->unsignedValues,
              QVector<quint64>({2, 7}));
}
