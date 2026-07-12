#include <gtest/gtest.h>

#include <pcl/common/centroid.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "CloudLoader.h"

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
