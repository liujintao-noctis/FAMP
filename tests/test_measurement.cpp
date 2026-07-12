#include <gtest/gtest.h>

#include <limits>
#include <algorithm>

#include "Measurement.h"
#include "MeasurementItem.h"

TEST(MeasurementTest, ConvertsAnisotropicSceneCoordinatesToMeters)
{
    const QVector<QPointF> scenePoints{
        QPointF(20.0, 60.0),
        QPointF(50.0, 100.0)};
    QVector<QPointF> meterPoints;
    QString error;

    ASSERT_TRUE(famp::measurement::sceneToMeters(
        scenePoints, QPointF(10.0, 20.0), meterPoints, &error));
    ASSERT_EQ(meterPoints.size(), 2);
    EXPECT_DOUBLE_EQ(meterPoints.at(0).x(), 2.0);
    EXPECT_DOUBLE_EQ(meterPoints.at(0).y(), 3.0);
    EXPECT_DOUBLE_EQ(meterPoints.at(1).x(), 5.0);
    EXPECT_DOUBLE_EQ(meterPoints.at(1).y(), 5.0);
    EXPECT_TRUE(error.isEmpty());
}

TEST(MeasurementTest, ComputesPolylineLength)
{
    const QVector<QPointF> points{
        QPointF(0.0, 0.0),
        QPointF(3.0, 4.0),
        QPointF(3.0, 8.0)};
    EXPECT_DOUBLE_EQ(famp::measurement::polylineLength(points), 9.0);
}

TEST(MeasurementTest, ComputesPolygonAreaRegardlessOfWinding)
{
    const QVector<QPointF> clockwise{
        QPointF(0.0, 0.0),
        QPointF(0.0, 4.0),
        QPointF(3.0, 4.0),
        QPointF(3.0, 0.0)};
    QVector<QPointF> counterClockwise = clockwise;
    std::reverse(counterClockwise.begin(), counterClockwise.end());

    EXPECT_DOUBLE_EQ(famp::measurement::polygonArea(clockwise), 12.0);
    EXPECT_DOUBLE_EQ(famp::measurement::polygonArea(counterClockwise), 12.0);
    EXPECT_DOUBLE_EQ(famp::measurement::polygonPerimeter(clockwise), 14.0);
}

TEST(MeasurementTest, ComputesAngleAtMiddlePoint)
{
    const QVector<QPointF> rightAngle{
        QPointF(1.0, 0.0), QPointF(0.0, 0.0), QPointF(0.0, 2.0)};
    EXPECT_NEAR(famp::measurement::angleDegrees(rightAngle), 90.0, 1.0e-12);
    EXPECT_DOUBLE_EQ(
        famp::measurement::value(famp::measurement::Kind::Angle, rightAngle),
        90.0);
    EXPECT_EQ(famp::measurement::formatSummary(
                  famp::measurement::Kind::Angle, rightAngle),
              QStringLiteral("90.00°"));
}

TEST(MeasurementTest, AreaSummaryIncludesPerimeter)
{
    const QVector<QPointF> rectangle{
        QPointF(0.0, 0.0), QPointF(3.0, 0.0),
        QPointF(3.0, 4.0), QPointF(0.0, 4.0)};
    const QString summary = famp::measurement::formatSummary(
        famp::measurement::Kind::Area, rectangle);
    EXPECT_TRUE(summary.contains(QStringLiteral("12.000 m²")));
    EXPECT_TRUE(summary.contains(QStringLiteral("14.000 m")));
}

TEST(MeasurementTest, RejectsInvalidScaleWithoutMutatingOutput)
{
    QVector<QPointF> output{QPointF(7.0, 8.0)};
    QString error;
    EXPECT_FALSE(famp::measurement::sceneToMeters(
        {QPointF(1.0, 2.0)}, QPointF(0.0, 10.0), output, &error));
    ASSERT_EQ(output.size(), 1);
    EXPECT_EQ(output.front(), QPointF(7.0, 8.0));
    EXPECT_FALSE(error.isEmpty());

    EXPECT_EQ(famp::measurement::formatValue(
                  famp::measurement::Kind::Distance,
                  std::numeric_limits<double>::infinity()),
              QStringLiteral("无效测量"));
}

TEST(MeasurementTest, FormatsLargeValuesUsingReadableUnits)
{
    EXPECT_EQ(famp::measurement::formatValue(
                  famp::measurement::Kind::Distance, 1500.0),
              QStringLiteral("1.500 km"));
    EXPECT_EQ(famp::measurement::formatValue(
                  famp::measurement::Kind::Area, 25000.0),
              QStringLiteral("2.500 ha"));
}

TEST(MeasurementItemTest, RescalesGeometryWithoutChangingMeasuredValue)
{
    MeasurementItem item(
        famp::measurement::Kind::Distance,
        {QPointF(0.0, 0.0), QPointF(3.0, 4.0)},
        QPointF(10.0, 10.0));

    const QRectF originalBounds = item.boundingRect();
    EXPECT_DOUBLE_EQ(item.value(), 5.0);

    item.setSceneUnitsPerMeter(QPointF(40.0, 40.0));
    EXPECT_DOUBLE_EQ(item.value(), 5.0);
    EXPECT_GT(item.boundingRect().width(), originalBounds.width());
    EXPECT_GT(item.boundingRect().height(), originalBounds.height());
}
