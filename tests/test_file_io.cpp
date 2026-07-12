#include <gtest/gtest.h>

#include <QDir>
#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include "FileIO.h"
#include "PcdLoader.h"

TEST(FileIOTest, EnforcesRequiredSuffixCaseInsensitively)
{
    EXPECT_EQ(famp::io::pathWithRequiredSuffix(QStringLiteral("map"), QStringLiteral(".bmp")),
              QStringLiteral("map.bmp"));
    EXPECT_EQ(famp::io::pathWithRequiredSuffix(QStringLiteral("map.BMP"), QStringLiteral("bmp")),
              QStringLiteral("map.BMP"));
    EXPECT_EQ(famp::io::pathWithRequiredSuffix(QStringLiteral("map.png"), QStringLiteral("bmp")),
              QStringLiteral("map.png.bmp"));
    EXPECT_TRUE(famp::io::pathWithRequiredSuffix({}, QStringLiteral("pcd")).isEmpty());
}

TEST(FileIOTest, SavesImageAtomicallyToNonAsciiPath)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir(directory.path()).mkpath(QStringLiteral("成果")));
    const QString path = directory.filePath(QStringLiteral("成果/平面图.bmp"));

    QImage image(4, 3, QImage::Format_ARGB32);
    image.fill(QColor(12, 34, 56));
    QString error;
    ASSERT_TRUE(famp::io::saveImageAtomically(path, image, "BMP", 100, &error))
        << error.toStdString();

    const QImage loaded(path);
    ASSERT_FALSE(loaded.isNull());
    EXPECT_EQ(loaded.size(), image.size());
    EXPECT_EQ(loaded.pixelColor(0, 0), image.pixelColor(0, 0));
}

TEST(FileIOTest, SavesPcdWithColorsToNonAsciiPath)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("开方.pcd"));

    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    pcl::PointXYZRGB point;
    point.x = 1.25F;
    point.y = -2.5F;
    point.z = 3.75F;
    point.r = 17;
    point.g = 34;
    point.b = 51;
    cloud.push_back(point);

    QString error;
    ASSERT_TRUE(famp::io::savePcdAsciiAtomically(path, cloud, &error))
        << error.toStdString();

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    ASSERT_TRUE(loadPcdAsRgb(path, loaded, &error)) << error.toStdString();
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->size(), 1);
    EXPECT_FLOAT_EQ(loaded->front().x, point.x);
    EXPECT_FLOAT_EQ(loaded->front().y, point.y);
    EXPECT_FLOAT_EQ(loaded->front().z, point.z);
    EXPECT_EQ(loaded->front().r, point.r);
    EXPECT_EQ(loaded->front().g, point.g);
    EXPECT_EQ(loaded->front().b, point.b);
}

TEST(FileIOTest, RejectsEmptyOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    QString error;
    const pcl::PointCloud<pcl::PointXYZRGB> emptyCloud;
    EXPECT_FALSE(famp::io::savePcdAsciiAtomically(
        directory.filePath(QStringLiteral("empty.pcd")), emptyCloud, &error));
    EXPECT_FALSE(error.isEmpty());

    error.clear();
    EXPECT_FALSE(famp::io::saveImageAtomically(
        directory.filePath(QStringLiteral("empty.bmp")), QImage(), "BMP", 100, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(FileIOTest, ReportsUnwritableDestinationWithoutLeavingAFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("missing/result.pcd"));

    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    cloud.push_back(pcl::PointXYZRGB());
    QString error;
    EXPECT_FALSE(famp::io::savePcdAsciiAtomically(path, cloud, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(path));
}
