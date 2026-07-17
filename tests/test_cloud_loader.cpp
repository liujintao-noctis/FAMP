#include <gtest/gtest.h>

#include <pcl/common/centroid.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "CloudLoader.h"
#include "FileIO.h"

#include <cmath>

#ifndef FAMP_SAMPLE_DIR
#error "FAMP_SAMPLE_DIR must point to the repository samples directory"
#endif

TEST(CloudLoaderTest, LoadsAndRecentersPcd)
{
    const QString path = QStringLiteral(FAMP_SAMPLE_DIR "/projectPointCloud.pcd");
    const famp::cloud::LoadResult result = famp::cloud::load(path);

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    ASSERT_TRUE(result.sourceWasPcd);
    ASSERT_TRUE(result.sourceCloud);
    ASSERT_TRUE(result.displayCloud);
    EXPECT_EQ(result.sourceCloud->size(), result.displayCloud->size());

    Eigen::Vector4f centroid;
    pcl::compute3DCentroid(*result.displayCloud, centroid);
    EXPECT_NEAR(centroid.x(), 0.0f, 1.0e-3f);
    EXPECT_NEAR(centroid.y(), 0.0f, 1.0e-3f);
    EXPECT_NEAR(centroid.z(), 0.0f, 1.0e-3f);
    EXPECT_TRUE(std::isfinite(result.spatial.origin[0]));
    EXPECT_TRUE(std::isfinite(result.spatial.origin[1]));
    EXPECT_TRUE(std::isfinite(result.spatial.origin[2]));
    ASSERT_FALSE(result.sourceCloud->empty());
    EXPECT_NEAR(
        static_cast<double>(result.displayCloud->front().x)
            + result.spatial.origin[0],
        static_cast<double>(result.sourceCloud->front().x), 1.0e-5);
    EXPECT_NEAR(
        static_cast<double>(result.displayCloud->front().y)
            + result.spatial.origin[1],
        static_cast<double>(result.sourceCloud->front().y), 1.0e-5);
    EXPECT_NEAR(
        static_cast<double>(result.displayCloud->front().z)
            + result.spatial.origin[2],
        static_cast<double>(result.sourceCloud->front().z), 1.0e-5);
}

TEST(CloudLoaderTest, LoadsLasWithoutKeepingDuplicateSource)
{
    const QString path = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    const famp::cloud::LoadResult result = famp::cloud::load(path);

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_FALSE(result.sourceWasPcd);
    EXPECT_FALSE(result.sourceCloud);
    EXPECT_FALSE(result.displayCloud->empty());
    EXPECT_TRUE(result.attributes.contains(QStringLiteral("intensity")));
    EXPECT_TRUE(result.attributes.contains(QStringLiteral("classification")));
    EXPECT_TRUE(result.attributes.validate(
        static_cast<qint64>(result.displayCloud->size())));
    EXPECT_TRUE(std::isfinite(result.spatial.origin[0]));
    EXPECT_TRUE(std::isfinite(result.spatial.origin[1]));
    EXPECT_TRUE(std::isfinite(result.spatial.origin[2]));
}

TEST(CloudLoaderTest, SupportsUnicodePath)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = QStringLiteral(FAMP_SAMPLE_DIR "/projectPointCloud.pcd");
    const QString destination = directory.filePath(QStringLiteral("探方点云.pcd"));
    ASSERT_TRUE(QFile::copy(source, destination));

    const famp::cloud::LoadResult result = famp::cloud::load(destination);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.path, QFileInfo(destination).absoluteFilePath());
}

TEST(CloudLoaderTest, RejectsMissingAndUnsupportedFiles)
{
    QString normalizedPath;
    QString error;
    EXPECT_FALSE(famp::cloud::validatePath(
        QStringLiteral("/missing/cloud.pcd"), &normalizedPath, &error));
    EXPECT_FALSE(error.isEmpty());

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString unsupported = directory.filePath(QStringLiteral("cloud.txt"));
    QFile file(unsupported);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not a cloud");
    file.close();

    const famp::cloud::LoadResult result = famp::cloud::load(unsupported);
    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.error.isEmpty());
    EXPECT_FALSE(result.displayCloud);
}

TEST(CloudLoaderTest, CancelsBeforeAllocatingDisplayCloud)
{
    const QString path = QStringLiteral(FAMP_SAMPLE_DIR "/projectPointCloud.pcd");
    const famp::cloud::LoadResult result =
        famp::cloud::load(path, []() { return true; });

    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.displayCloud);
    EXPECT_FALSE(result.sourceCloud);
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(CloudLoaderTest, LoadsAndRecentersUnicodeXyz)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("探方坐标.xyz"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("10 20 30 1 2 3\n14 24 34 4 5 6\n");
    file.close();

    const famp::cloud::LoadResult result = famp::cloud::load(path);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    ASSERT_EQ(result.displayCloud->size(), 2U);
    EXPECT_NEAR(result.spatial.origin[0], 12.0, 1.0e-12);
    EXPECT_NEAR(result.spatial.origin[1], 22.0, 1.0e-12);
    EXPECT_NEAR(result.spatial.origin[2], 32.0, 1.0e-12);
    EXPECT_FLOAT_EQ(result.displayCloud->front().x, -2.0f);
    EXPECT_FLOAT_EQ(result.displayCloud->back().z, 2.0f);
    EXPECT_EQ(result.displayCloud->front().r, 1);
}

TEST(CloudLoaderTest, PreservesEmbeddedSpatialAndLocalPcdCoordinates)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("局部成果.pcd"));
    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    pcl::PointXYZRGB first;
    first.x = 5.0F;
    first.y = 7.0F;
    first.z = 9.0F;
    first.r = 10;
    first.g = 20;
    first.b = 30;
    cloud.push_back(first);
    pcl::PointXYZRGB second = first;
    second.x = 8.0F;
    cloud.push_back(second);
    famp::cloud::SpatialReference spatial;
    spatial.origin = {123456.25, 3456789.5, 88.75};
    spatial.transform[3] = 12.5;
    QString error;
    ASSERT_TRUE(famp::io::savePcdAsciiAtomically(
        path, cloud, &error, &spatial)) << error.toStdString();

    const auto result = famp::cloud::load(path);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    ASSERT_EQ(result.displayCloud->size(), 2U);
    EXPECT_FLOAT_EQ((*result.displayCloud)[0].x, 5.0F);
    EXPECT_FLOAT_EQ((*result.displayCloud)[1].x, 8.0F);
    EXPECT_EQ(result.spatial.origin, spatial.origin);
    EXPECT_EQ(result.spatial.transform, spatial.transform);
}
