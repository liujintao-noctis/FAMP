#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include "CloudLayer.h"

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr sampleCloud()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    cloud->resize(2);
    cloud->points[0].x = 1.0F;
    cloud->points[1].x = 2.0F;
    return cloud;
}
}

TEST(CloudLayerTest, BuildsValidLayerWithStableRuntimeIdentity)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("探方点云.pcd"));
    auto layer = famp::cloud::makeLayer(path, sampleCloud());

    EXPECT_TRUE(famp::cloud::isValidLayerId(layer.id));
    EXPECT_EQ(layer.name, QStringLiteral("探方点云.pcd"));
    EXPECT_EQ(layer.sourcePath, QDir::cleanPath(path));
    EXPECT_EQ(layer.pointCount(), 2U);
    QString error;
    EXPECT_TRUE(famp::cloud::validateLayer(layer, true, &error))
        << error.toStdString();
}

TEST(CloudLayerTest, GeneratesDeterministicMigrationIdentity)
{
    const QString first = famp::cloud::stableLayerId(
        QStringLiteral("/data/site/cloud.pcd"));
    const QString second = famp::cloud::stableLayerId(
        QStringLiteral("/data/site/cloud.pcd"));
    const QString different = famp::cloud::stableLayerId(
        QStringLiteral("/data/site/other.pcd"));
    EXPECT_EQ(first, second);
    EXPECT_NE(first, different);
    EXPECT_TRUE(famp::cloud::isValidLayerId(first));
}

TEST(CloudLayerTest, RejectsAttributeCountAndUnsafeMetadata)
{
    auto layer = famp::cloud::makeLayer(QStringLiteral("site.pcd"), sampleCloud());
    famp::cloud::AttributeChannel channel;
    channel.name = QStringLiteral("intensity");
    channel.floatingValues = {1.0};
    ASSERT_TRUE(layer.attributes.insert(channel));

    QString error;
    EXPECT_FALSE(famp::cloud::validateLayer(layer, true, &error));
    EXPECT_FALSE(error.isEmpty());

    layer.attributes.clear();
    layer.archaeologyFields.insert(QString(), QStringLiteral("invalid"));
    EXPECT_FALSE(famp::cloud::validateLayer(layer, true, &error));

    layer.archaeologyFields.clear();
    famp::control::Point controlPoint;
    controlPoint.id = famp::control::createPointId();
    controlPoint.name = QStringLiteral("CP-1");
    layer.controlPoints = {controlPoint, controlPoint};
    EXPECT_FALSE(famp::cloud::validateLayer(layer, true, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("ID")));
}

TEST(CloudLayerTest, AllowsMetadataOnlyLayerWhenExplicitlyRequested)
{
    famp::cloud::CloudLayer layer;
    layer.id = famp::cloud::createLayerId();
    layer.name = QStringLiteral("等待载入的点云");
    EXPECT_TRUE(famp::cloud::validateLayer(layer, false));
    EXPECT_FALSE(famp::cloud::validateLayer(layer, true));
}
