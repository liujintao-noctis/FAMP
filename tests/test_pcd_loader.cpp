#include <gtest/gtest.h>

#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QDir>
#include <QFile>

#include "PcdLoader.h"

#include <cstdio>
#include <fstream>
#include <limits>

TEST(PcdLoaderTest, LoadsXyzOnlyPcdAsRgbCloud)
{
    pcl::PointCloud<pcl::PointXYZ> xyzCloud;
    xyzCloud.width = 2;
    xyzCloud.height = 1;
    xyzCloud.is_dense = true;
    xyzCloud.push_back({1.0f, 2.0f, 3.0f});
    xyzCloud.push_back({4.0f, 5.0f, 6.0f});

    const std::string path = "test_xyz_only_loader.pcd";
    ASSERT_EQ(pcl::io::savePCDFileBinary(path, xyzCloud), 0);

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    std::string error;
    EXPECT_TRUE(loadPcdAsRgb(path, loaded, &error)) << error;
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->size(), 2U);
    EXPECT_FLOAT_EQ((*loaded)[0].x, 1.0f);
    EXPECT_FLOAT_EQ((*loaded)[0].y, 2.0f);
    EXPECT_FLOAT_EQ((*loaded)[0].z, 3.0f);
    EXPECT_EQ((*loaded)[0].r, 255);
    EXPECT_EQ((*loaded)[0].g, 255);
    EXPECT_EQ((*loaded)[0].b, 255);

    std::remove(path.c_str());
}

TEST(PcdLoaderTest, LoadsLegacyPaddingFieldPcdAsRgbCloud)
{
    const std::string path = "test_xyz_padding_loader.pcd";
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
    file << "# .PCD v0.7 - Point Cloud Data file format\n"
         << "VERSION 0.7\n"
         << "FIELDS x y z _\n"
         << "SIZE 4 4 4 1\n"
         << "TYPE F F F U\n"
         << "COUNT 1 1 1 4\n"
         << "WIDTH 2\n"
         << "HEIGHT 1\n"
         << "VIEWPOINT 0 0 0 1 0 0 0\n"
         << "POINTS 2\n"
         << "DATA ascii\n"
         << "1 2 3 0 0 0 0\n"
         << "4 5 6 0 0 0 0\n";
    file.close();

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    std::string error;
    EXPECT_TRUE(loadPcdAsRgb(path, loaded, &error)) << error;
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->size(), 2U);
    EXPECT_FLOAT_EQ((*loaded)[1].x, 4.0f);
    EXPECT_FLOAT_EQ((*loaded)[1].y, 5.0f);
    EXPECT_FLOAT_EQ((*loaded)[1].z, 6.0f);
    EXPECT_EQ((*loaded)[1].r, 255);
    EXPECT_EQ((*loaded)[1].g, 255);
    EXPECT_EQ((*loaded)[1].b, 255);

    std::remove(path.c_str());
}

TEST(PcdLoaderTest, LoadsQStringPathWithNonAsciiFileName)
{
    pcl::PointCloud<pcl::PointXYZ> xyzCloud;
    xyzCloud.width = 1;
    xyzCloud.height = 1;
    xyzCloud.is_dense = true;
    xyzCloud.push_back({7.0f, 8.0f, 9.0f});

    const std::string asciiPath = "test_non_ascii_source.pcd";
    ASSERT_EQ(pcl::io::savePCDFileBinary(asciiPath, xyzCloud), 0);

    const QString path = QDir::tempPath() + QStringLiteral("/大墓坑_对齐_sampling_25000_test.pcd");
    QFile::remove(path);
    ASSERT_TRUE(QFile::copy(QString::fromStdString(asciiPath), path));

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    QString error;
    EXPECT_TRUE(loadPcdAsRgb(path, loaded, &error)) << error.toStdString();
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->size(), 1U);
    EXPECT_FLOAT_EQ((*loaded)[0].x, 7.0f);
    EXPECT_FLOAT_EQ((*loaded)[0].y, 8.0f);
    EXPECT_FLOAT_EQ((*loaded)[0].z, 9.0f);
    EXPECT_EQ((*loaded)[0].r, 255);
    EXPECT_EQ((*loaded)[0].g, 255);
    EXPECT_EQ((*loaded)[0].b, 255);

    QFile::remove(path);
    std::remove(asciiPath.c_str());
}

TEST(PcdLoaderTest, FiltersNonFinitePointsAndRejectsInvalidSpatialMetadata)
{
    const std::string path = "test_non_finite_loader.pcd";
    pcl::PointCloud<pcl::PointXYZRGB> source;
    pcl::PointXYZRGB valid;
    valid.x = 1.0F;
    valid.y = 2.0F;
    valid.z = 3.0F;
    source.push_back(valid);
    pcl::PointXYZRGB invalid = valid;
    invalid.x = std::numeric_limits<float>::quiet_NaN();
    source.push_back(invalid);
    ASSERT_EQ(pcl::io::savePCDFileASCII(path, source), 0);
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    std::string error;
    ASSERT_TRUE(loadPcdAsRgb(path, loaded, &error)) << error;
    ASSERT_EQ(loaded->size(), 1U);
    EXPECT_FLOAT_EQ(loaded->front().x, 1.0F);
    std::remove(path.c_str());

    const QString malformed = QDir::tempPath()
        + QStringLiteral("/test_invalid_famp_spatial.pcd");
    QFile file(malformed);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("# .PCD v0.7\n# FAMP_SPATIAL_REFERENCE 1\n"
               "# FAMP_ORIGIN 0 0 nan\n"
               "# FAMP_TRANSFORM 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
               "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\n"
               "COUNT 1 1 1\nWIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n0 0 0\n");
    file.close();
    QString qtError;
    EXPECT_FALSE(loadPcdAsRgb(malformed, loaded, &qtError));
    EXPECT_TRUE(qtError.contains(QStringLiteral("空间参考")));
    QFile::remove(malformed);
}
