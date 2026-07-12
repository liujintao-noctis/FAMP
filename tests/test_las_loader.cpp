#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "LasLoader.h"

#include <cmath>

#ifndef FAMP_SAMPLE_DIR
#error "FAMP_SAMPLE_DIR must point to the repository samples directory"
#endif

TEST(LasLoaderTest, LoadsBundledSampleAndRecentersCoordinates)
{
    const QString path = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    std::array<double, 3> origin{};
    QString error;

    ASSERT_TRUE(loadLasAsRgb(path, cloud, &error, &origin)) << error.toStdString();
    ASSERT_TRUE(cloud);
    EXPECT_FALSE(cloud->empty());
    EXPECT_EQ(cloud->height, 1U);
    EXPECT_EQ(cloud->width, cloud->size());
    EXPECT_TRUE(std::isfinite(origin[0]));
    EXPECT_TRUE(std::isfinite(origin[1]));
    EXPECT_TRUE(std::isfinite(origin[2]));
}

TEST(LasLoaderTest, LoadsFromNonAsciiPath)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    const QString destination = directory.filePath(QStringLiteral("探方一.las"));
    ASSERT_TRUE(QFile::copy(source, destination));

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    QString error;
    ASSERT_TRUE(loadLasAsRgb(destination, cloud, &error)) << error.toStdString();
    ASSERT_TRUE(cloud);
    EXPECT_FALSE(cloud->empty());
}

TEST(LasLoaderTest, RejectsMissingFileWithoutMutatingOutput)
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr original(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    original->push_back(pcl::PointXYZRGB());
    auto output = original;
    QString error;

    EXPECT_FALSE(loadLasAsRgb(QStringLiteral("/missing/input.las"), output, &error));
    EXPECT_EQ(output, original);
    EXPECT_FALSE(error.isEmpty());
}
