#include <gtest/gtest.h>

#include "CrsService.h"

#include <limits>

TEST(CrsServiceTest, NormalizesCommonEpsgForms)
{
    EXPECT_EQ(famp::crs::normalizedEpsg(QStringLiteral("epsg: 4326")),
              QStringLiteral("EPSG:4326"));
    EXPECT_EQ(famp::crs::normalizedEpsg(QStringLiteral("4490")),
              QStringLiteral("EPSG:4490"));
    EXPECT_TRUE(famp::crs::normalizedEpsg(QStringLiteral("EPSG:abc")).isEmpty());
}

TEST(CrsServiceTest, InspectsKnownAndRejectsUnknownCrs)
{
    famp::crs::Info info;
    QString error;
    ASSERT_TRUE(famp::crs::inspect(QStringLiteral("EPSG:4326"), info, &error))
        << error.toStdString();
    EXPECT_EQ(info.identifier, QStringLiteral("EPSG:4326"));
    EXPECT_TRUE(info.name.contains(QStringLiteral("WGS 84")));
    EXPECT_TRUE(info.geographic);
    EXPECT_FALSE(info.projected);

    ASSERT_TRUE(famp::crs::inspect(QStringLiteral("EPSG:3857"), info, &error))
        << error.toStdString();
    EXPECT_TRUE(info.projected);
    EXPECT_FALSE(info.geographic);
    EXPECT_FALSE(info.horizontalUnitName.isEmpty());
    EXPECT_DOUBLE_EQ(info.horizontalUnitToMetre, 1.0);

    const famp::crs::Info original = info;
    EXPECT_FALSE(famp::crs::inspect(QStringLiteral("EPSG:99999999"), info, &error));
    EXPECT_EQ(info.identifier, original.identifier);
    EXPECT_FALSE(error.isEmpty());
}

TEST(CrsServiceTest, TransformsWgs84ToWebMercator)
{
    const famp::crs::Coordinate source{12.0, 55.0, 7.0};
    famp::crs::Coordinate target;
    QString error;
    ASSERT_TRUE(famp::crs::transform(
        QStringLiteral("EPSG:4326"),
        QStringLiteral("EPSG:3857"),
        source,
        target,
        &error)) << error.toStdString();

    EXPECT_NEAR(target.x, 1335833.8895, 0.01);
    EXPECT_NEAR(target.y, 7361866.1131, 0.01);
    EXPECT_NEAR(target.z, source.z, 1e-9);
}

TEST(CrsServiceTest, RejectsNonFiniteCoordinateWithoutMutatingOutput)
{
    const famp::crs::Coordinate original{1.0, 2.0, 3.0};
    auto target = original;
    QString error;
    EXPECT_FALSE(famp::crs::transform(
        QStringLiteral("EPSG:4326"),
        QStringLiteral("EPSG:3857"),
        {std::numeric_limits<double>::infinity(), 0.0, 0.0},
        target,
        &error));
    EXPECT_DOUBLE_EQ(target.x, original.x);
    EXPECT_DOUBLE_EQ(target.y, original.y);
    EXPECT_DOUBLE_EQ(target.z, original.z);
    EXPECT_FALSE(error.isEmpty());
}

TEST(CrsServiceTest, ReusesInitializedTransformerAndMovesSafely)
{
    famp::crs::Transformer transformer;
    QString error;
    ASSERT_TRUE(transformer.initialize(
        QStringLiteral("EPSG:4326"), QStringLiteral("EPSG:3857"), &error))
        << error.toStdString();
    EXPECT_TRUE(transformer.isValid());
    EXPECT_EQ(transformer.sourceIdentifier(), QStringLiteral("EPSG:4326"));
    EXPECT_EQ(transformer.targetIdentifier(), QStringLiteral("EPSG:3857"));

    famp::crs::Coordinate first;
    famp::crs::Coordinate second;
    ASSERT_TRUE(transformer.transform({12.0, 55.0, 0.0}, first, &error));
    ASSERT_TRUE(transformer.transform({13.0, 55.0, 0.0}, second, &error));
    EXPECT_GT(second.x, first.x);

    famp::crs::Transformer moved(std::move(transformer));
    EXPECT_FALSE(transformer.isValid());
    EXPECT_TRUE(moved.isValid());
    EXPECT_FALSE(transformer.transform({12.0, 55.0, 0.0}, second, &error));
    EXPECT_FALSE(error.isEmpty());
}
