#include <gtest/gtest.h>

#include <QDir>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

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

TEST(FileIOTest, RoundTripsTypedPointAttributesWithoutPrecisionLoss)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("属性成果.pcd"));

    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    for (int index = 0; index < 3; ++index)
    {
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(index + 1);
        point.y = static_cast<float>(index + 2);
        point.z = static_cast<float>(index + 3);
        point.r = static_cast<std::uint8_t>(10 + index);
        point.g = static_cast<std::uint8_t>(20 + index);
        point.b = static_cast<std::uint8_t>(30 + index);
        cloud.push_back(point);
    }

    famp::cloud::CloudAttributes attributes;
    famp::cloud::AttributeChannel floating;
    floating.name = QStringLiteral("高程残差");
    floating.unit = QStringLiteral("毫米 mm");
    floating.type = famp::cloud::AttributeValueType::Float64;
    floating.floatingValues = {
        1.2345678901234567,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()};
    QString error;
    ASSERT_TRUE(attributes.insert(std::move(floating), 3, &error))
        << error.toStdString();

    famp::cloud::AttributeChannel signedIntegers;
    signedIntegers.name = QStringLiteral("signed extrema");
    signedIntegers.type = famp::cloud::AttributeValueType::SignedInteger;
    signedIntegers.signedValues = {
        std::numeric_limits<qint64>::min(), -1,
        std::numeric_limits<qint64>::max()};
    ASSERT_TRUE(attributes.insert(std::move(signedIntegers), 3, &error))
        << error.toStdString();

    famp::cloud::AttributeChannel unsignedIntegers;
    unsignedIntegers.name = QStringLiteral("unsigned extrema");
    unsignedIntegers.unit = QStringLiteral("count");
    unsignedIntegers.type = famp::cloud::AttributeValueType::UnsignedInteger;
    unsignedIntegers.unsignedValues = {
        0, UINT64_C(9007199254740993),
        std::numeric_limits<quint64>::max()};
    ASSERT_TRUE(attributes.insert(std::move(unsignedIntegers), 3, &error))
        << error.toStdString();

    famp::cloud::SpatialReference spatial;
    spatial.origin = {123456.125, 3456789.25, 87.5};
    ASSERT_TRUE(famp::io::savePcdAsciiAtomically(
        path, cloud, &error, &spatial, &attributes)) << error.toStdString();

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    famp::cloud::SpatialReference loadedSpatial;
    bool hasEmbeddedSpatial = false;
    famp::cloud::CloudAttributes loadedAttributes;
    ASSERT_TRUE(loadPcdAsRgb(
        path, loaded, &error, &loadedSpatial, &hasEmbeddedSpatial,
        &loadedAttributes)) << error.toStdString();
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->size(), cloud.size());
    EXPECT_TRUE(hasEmbeddedSpatial);
    EXPECT_EQ(loadedSpatial.origin, spatial.origin);
    ASSERT_TRUE(loadedAttributes.validate(3, &error)) << error.toStdString();
    ASSERT_EQ(loadedAttributes.size(), 3);

    const auto* loadedFloating = loadedAttributes.channel(
        QStringLiteral("高程残差"));
    ASSERT_NE(loadedFloating, nullptr);
    EXPECT_EQ(loadedFloating->unit, QStringLiteral("毫米 mm"));
    ASSERT_EQ(loadedFloating->floatingValues.size(), 3);
    EXPECT_DOUBLE_EQ(loadedFloating->floatingValues.at(0),
                     1.2345678901234567);
    EXPECT_TRUE(std::isnan(loadedFloating->floatingValues.at(1)));
    EXPECT_TRUE(std::isinf(loadedFloating->floatingValues.at(2)));
    EXPECT_GT(loadedFloating->floatingValues.at(2), 0.0);

    const auto* loadedSigned = loadedAttributes.channel(
        QStringLiteral("signed extrema"));
    ASSERT_NE(loadedSigned, nullptr);
    EXPECT_EQ(loadedSigned->signedValues,
              QVector<qint64>({std::numeric_limits<qint64>::min(), -1,
                               std::numeric_limits<qint64>::max()}));

    const auto* loadedUnsigned = loadedAttributes.channel(
        QStringLiteral("unsigned extrema"));
    ASSERT_NE(loadedUnsigned, nullptr);
    EXPECT_EQ(loadedUnsigned->unsignedValues,
              QVector<quint64>({0, UINT64_C(9007199254740993),
                                std::numeric_limits<quint64>::max()}));
}

TEST(FileIOTest, RejectsMalformedFampAttributeMetadataAtomically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("损坏属性.pcd"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(
        "# .PCD v0.7 - Point Cloud Data file format\n"
        "# FAMP_ATTRIBUTES 1 1\n"
        "# FAMP_ATTRIBUTE 0 famp_attr_0 bYQ b float64\n"
        "VERSION 0.7\n"
        "FIELDS x y z rgb\n"
        "SIZE 4 4 4 4\n"
        "TYPE F F F U\n"
        "COUNT 1 1 1 1\n"
        "WIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n"
        "0 0 0 0\n");
    file.close();

    auto originalCloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    originalCloud->push_back(pcl::PointXYZRGB());
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr output = originalCloud;
    famp::cloud::CloudAttributes outputAttributes;
    famp::cloud::AttributeChannel preserved;
    preserved.name = QStringLiteral("preserved");
    preserved.type = famp::cloud::AttributeValueType::SignedInteger;
    preserved.signedValues = {7};
    QString error;
    ASSERT_TRUE(outputAttributes.insert(std::move(preserved), 1, &error));

    EXPECT_FALSE(loadPcdAsRgb(
        path, output, &error, nullptr, nullptr, &outputAttributes));
    EXPECT_EQ(output, originalCloud);
    EXPECT_TRUE(outputAttributes.contains(QStringLiteral("preserved")));
    EXPECT_TRUE(error.contains(QStringLiteral("逐点属性元数据无效")));
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

TEST(FileIOTest, RejectsInvalidCoordinatesAndSpatialReferenceBeforeWriting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    pcl::PointXYZRGB point;
    point.x = std::numeric_limits<float>::quiet_NaN();
    point.y = 0.0F;
    point.z = 0.0F;
    cloud.push_back(point);
    QString error;
    const QString invalidPointPath = directory.filePath(QStringLiteral("point.pcd"));
    EXPECT_FALSE(famp::io::savePcdAsciiAtomically(
        invalidPointPath, cloud, &error));
    EXPECT_FALSE(QFileInfo::exists(invalidPointPath));

    point.x = 0.0F;
    cloud[0] = point;
    famp::cloud::SpatialReference spatial;
    spatial.origin[0] = std::numeric_limits<double>::infinity();
    const QString invalidSpatialPath = directory.filePath(QStringLiteral("spatial.pcd"));
    EXPECT_FALSE(famp::io::savePcdAsciiAtomically(
        invalidSpatialPath, cloud, &error, &spatial));
    EXPECT_FALSE(QFileInfo::exists(invalidSpatialPath));
}
