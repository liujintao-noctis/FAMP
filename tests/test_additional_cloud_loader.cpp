#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "AdditionalCloudLoader.h"

TEST(AdditionalCloudLoaderTest, LoadsUnicodePlyWithoutColors)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("遗址.ply"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("ply\nformat ascii 1.0\nelement vertex 2\n"
               "property float x\nproperty float y\nproperty float z\n"
               "end_header\n0 1 2\n3 4 5\n");
    file.close();

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    QString error;
    ASSERT_TRUE(loadPlyAsRgb(path, cloud, &error)) << error.toStdString();
    ASSERT_EQ(cloud->size(), 2U);
    EXPECT_EQ(cloud->front().r, 255);
    EXPECT_FLOAT_EQ(cloud->back().z, 5.0f);
}

TEST(AdditionalCloudLoaderTest, LoadsXyzWithOptionalRgbAndSeparators)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("测点.xyz"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("# x y z [r g b]\n0,1,2,10,20,30\n3;4;5\n");
    file.close();

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    QString error;
    ASSERT_TRUE(loadXyzTextAsRgb(path, cloud, &error))
        << error.toStdString();
    ASSERT_EQ(cloud->size(), 2U);
    EXPECT_EQ(cloud->front().r, 10);
    EXPECT_EQ(cloud->front().g, 20);
    EXPECT_EQ(cloud->front().b, 30);
    EXPECT_EQ(cloud->back().r, 255);
}

TEST(AdditionalCloudLoaderTest, RejectsMalformedXyzAtomically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("bad.xyz"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("0 1 2\n3 invalid 5\n");
    file.close();

    auto preserved = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    preserved->push_back(pcl::PointXYZRGB{});
    auto output = preserved;
    QString error;
    EXPECT_FALSE(loadXyzTextAsRgb(path, output, &error));
    EXPECT_EQ(output, preserved);
    EXPECT_FALSE(error.isEmpty());
}

TEST(AdditionalCloudLoaderTest, IgnoresColorWordsOutsideVertexProperties)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("无色注释.ply"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("ply\nformat ascii 1.0\ncomment red\ncomment green\ncomment blue\n"
               "element vertex 1\nproperty float x\nproperty float y\n"
               "property float z\nend_header\n1 2 3\n");
    file.close();

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    QString error;
    ASSERT_TRUE(loadPlyAsRgb(path, cloud, &error)) << error.toStdString();
    ASSERT_EQ(cloud->size(), 1U);
    EXPECT_EQ(cloud->front().r, 255);
    EXPECT_EQ(cloud->front().g, 255);
    EXPECT_EQ(cloud->front().b, 255);
}
