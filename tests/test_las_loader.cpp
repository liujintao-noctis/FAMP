#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <lasreader.hpp>
#include <laswriter.hpp>

#include "CloudLoader.h"
#include "LasLoader.h"

#include <cmath>
#include <cstdint>
#include <memory>

#ifndef FAMP_SAMPLE_DIR
#error "FAMP_SAMPLE_DIR must point to the repository samples directory"
#endif

namespace
{
bool convertLasToLaz(const QString& sourcePath, const QString& targetPath)
{
    const QByteArray source = QFile::encodeName(sourcePath);
    LASreadOpener readOpener;
    readOpener.set_file_name(const_cast<char*>(source.constData()));
    LASreader* reader = readOpener.open();
    if (!reader)
        return false;

    const QByteArray target = QFile::encodeName(targetPath);
    LASwriteOpener writeOpener;
    writeOpener.set_file_name(const_cast<char*>(target.constData()));
    if (!writeOpener.set_format("laz"))
    {
        reader->close();
        delete reader;
        return false;
    }
    LASwriter* writer = writeOpener.open(&reader->header);
    if (!writer)
    {
        reader->close();
        delete reader;
        return false;
    }

    bool succeeded = true;
    while (reader->read_point())
    {
        if (!writer->write_point(&reader->point))
        {
            succeeded = false;
            break;
        }
    }
    writer->close();
    delete writer;
    reader->close();
    delete reader;
    return succeeded && QFileInfo(targetPath).size() > 0;
}
}

TEST(LasLoaderTest, LoadsBundledSampleAndRecentersCoordinates)
{
    const QString path = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    std::array<double, 3> origin{};
    famp::cloud::CloudAttributes attributes;
    QString error;

    ASSERT_TRUE(loadLasAsRgb(path, cloud, &error, &origin, &attributes))
        << error.toStdString();
    ASSERT_TRUE(cloud);
    EXPECT_FALSE(cloud->empty());
    EXPECT_EQ(cloud->height, 1U);
    EXPECT_EQ(cloud->width, cloud->size());
    EXPECT_TRUE(std::isfinite(origin[0]));
    EXPECT_TRUE(std::isfinite(origin[1]));
    EXPECT_TRUE(std::isfinite(origin[2]));
    ASSERT_TRUE(attributes.validate(static_cast<qint64>(cloud->size())));
    for (const QString& name : {
             QStringLiteral("intensity"),
             QStringLiteral("classification"),
             QStringLiteral("return_number"),
             QStringLiteral("number_of_returns"),
             QStringLiteral("scan_angle"),
             QStringLiteral("user_data"),
             QStringLiteral("point_source_id")})
    {
        ASSERT_TRUE(attributes.contains(name)) << name.toStdString();
        EXPECT_EQ(attributes.channel(name)->valueCount(),
                  static_cast<qint64>(cloud->size()));
    }
}

TEST(LasLoaderTest, LoadsActualCompressedLazWithAttributes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    const QString compressed = directory.filePath(QStringLiteral("sample.laz"));
    ASSERT_TRUE(convertLasToLaz(source, compressed));
    EXPECT_LT(QFileInfo(compressed).size(), QFileInfo(source).size());
    const QString unicodePath = directory.filePath(
        QStringLiteral("探方压缩点云.laz"));
    ASSERT_TRUE(QFile::copy(compressed, unicodePath));

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    std::array<double, 3> origin{};
    famp::cloud::CloudAttributes attributes;
    QString error;
    ASSERT_TRUE(loadLasAsRgb(
        unicodePath, cloud, &error, &origin, &attributes))
        << error.toStdString();
    ASSERT_TRUE(cloud);
    EXPECT_FALSE(cloud->empty());
    EXPECT_TRUE(attributes.validate(static_cast<qint64>(cloud->size())));

    const auto requireUnsignedChannel = [&attributes](
        const QString& name, const QString& unit = QString()) {
        const famp::cloud::AttributeChannel* channel = attributes.channel(name);
        EXPECT_NE(channel, nullptr) << name.toStdString();
        if (channel)
        {
            EXPECT_EQ(channel->type,
                      famp::cloud::AttributeValueType::UnsignedInteger);
            EXPECT_EQ(channel->unit, unit);
        }
        return channel;
    };
    const auto requireFloatChannel = [&attributes](
        const QString& name, const QString& unit) {
        const famp::cloud::AttributeChannel* channel = attributes.channel(name);
        EXPECT_NE(channel, nullptr) << name.toStdString();
        if (channel)
        {
            EXPECT_EQ(channel->type,
                      famp::cloud::AttributeValueType::Float64);
            EXPECT_EQ(channel->unit, unit);
        }
        return channel;
    };
    const auto* intensity = requireUnsignedChannel(
        QStringLiteral("intensity"), QStringLiteral("raw"));
    const auto* classification = requireUnsignedChannel(
        QStringLiteral("classification"));
    const auto* returnNumber = requireUnsignedChannel(
        QStringLiteral("return_number"));
    const auto* numberOfReturns = requireUnsignedChannel(
        QStringLiteral("number_of_returns"));
    const auto* scanAngle = requireFloatChannel(
        QStringLiteral("scan_angle"), QStringLiteral("degree"));
    const auto* userData = requireUnsignedChannel(QStringLiteral("user_data"));
    const auto* pointSourceId = requireUnsignedChannel(
        QStringLiteral("point_source_id"));
    ASSERT_NE(intensity, nullptr);
    ASSERT_NE(classification, nullptr);
    ASSERT_NE(returnNumber, nullptr);
    ASSERT_NE(numberOfReturns, nullptr);
    ASSERT_NE(scanAngle, nullptr);
    ASSERT_NE(userData, nullptr);
    ASSERT_NE(pointSourceId, nullptr);

    const QByteArray encodedCompressed = QFile::encodeName(compressed);
    LASreadOpener opener;
    opener.set_file_name(const_cast<char*>(encodedCompressed.constData()));
    auto closeReader = [](LASreader* reader) {
        if (reader)
        {
            reader->close();
            delete reader;
        }
    };
    std::unique_ptr<LASreader, decltype(closeReader)> reader(
        opener.open(), closeReader);
    ASSERT_TRUE(reader);
    const double centerX = reader->get_min_x()
        + (reader->get_max_x() - reader->get_min_x()) / 2.0;
    const double centerY = reader->get_min_y()
        + (reader->get_max_y() - reader->get_min_y()) / 2.0;
    const double centerZ = reader->get_min_z()
        + (reader->get_max_z() - reader->get_min_z()) / 2.0;
    EXPECT_DOUBLE_EQ(origin[0], centerX);
    EXPECT_DOUBLE_EQ(origin[1], centerY);
    EXPECT_DOUBLE_EQ(origin[2], centerZ);

    const bool hasGpsTime = reader->point.have_gps_time;
    const famp::cloud::AttributeChannel* gpsTime = nullptr;
    if (hasGpsTime)
    {
        gpsTime = requireFloatChannel(
            QStringLiteral("gps_time"), QStringLiteral("s"));
        ASSERT_NE(gpsTime, nullptr);
    }
    else
    {
        EXPECT_FALSE(attributes.contains(QStringLiteral("gps_time")));
    }

    std::size_t index = 0;
    while (reader->read_point())
    {
        ASSERT_LT(index, cloud->size());
        const pcl::PointXYZRGB& loadedPoint = cloud->points[index];
        EXPECT_FLOAT_EQ(loadedPoint.x,
                        static_cast<float>(reader->point.get_x() - centerX));
        EXPECT_FLOAT_EQ(loadedPoint.y,
                        static_cast<float>(reader->point.get_y() - centerY));
        EXPECT_FLOAT_EQ(loadedPoint.z,
                        static_cast<float>(reader->point.get_z() - centerZ));
        EXPECT_EQ(loadedPoint.r,
                  static_cast<std::uint8_t>(reader->point.get_R() >> 8));
        EXPECT_EQ(loadedPoint.g,
                  static_cast<std::uint8_t>(reader->point.get_G() >> 8));
        EXPECT_EQ(loadedPoint.b,
                  static_cast<std::uint8_t>(reader->point.get_B() >> 8));

        const bool extended = reader->point.is_extended_point_type();
        EXPECT_EQ(intensity->unsignedValues.at(static_cast<int>(index)),
                  reader->point.get_intensity());
        EXPECT_EQ(classification->unsignedValues.at(static_cast<int>(index)),
                  extended ? reader->point.get_extended_classification()
                           : reader->point.get_classification());
        EXPECT_EQ(returnNumber->unsignedValues.at(static_cast<int>(index)),
                  extended ? reader->point.get_extended_return_number()
                           : reader->point.get_return_number());
        EXPECT_EQ(numberOfReturns->unsignedValues.at(static_cast<int>(index)),
                  extended ? reader->point.get_extended_number_of_returns()
                           : reader->point.get_number_of_returns());
        EXPECT_DOUBLE_EQ(scanAngle->floatingValues.at(static_cast<int>(index)),
                         reader->point.get_scan_angle());
        EXPECT_EQ(userData->unsignedValues.at(static_cast<int>(index)),
                  reader->point.get_user_data());
        EXPECT_EQ(pointSourceId->unsignedValues.at(static_cast<int>(index)),
                  reader->point.get_point_source_ID());
        if (gpsTime)
        {
            EXPECT_DOUBLE_EQ(
                gpsTime->floatingValues.at(static_cast<int>(index)),
                reader->point.get_gps_time());
        }
        ++index;
    }
    EXPECT_EQ(index, cloud->size());

    const famp::cloud::LoadResult result = famp::cloud::load(unicodePath);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_TRUE(result.attributes.contains(QStringLiteral("classification")));
    EXPECT_EQ(result.displayCloud->size(), cloud->size());
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
    famp::cloud::AttributeChannel sentinel;
    sentinel.name = QStringLiteral("sentinel");
    sentinel.type = famp::cloud::AttributeValueType::UnsignedInteger;
    sentinel.unsignedValues = {42};
    famp::cloud::CloudAttributes attributes;
    ASSERT_TRUE(attributes.insert(sentinel, 1));
    QString error;

    EXPECT_FALSE(loadLasAsRgb(
        QStringLiteral("/missing/input.las"), output, &error, nullptr,
        &attributes));
    EXPECT_EQ(output, original);
    EXPECT_TRUE(attributes.contains(QStringLiteral("sentinel")));
    EXPECT_FALSE(error.isEmpty());
}
