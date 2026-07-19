#include <gtest/gtest.h>

#include "MetricGrid.h"
#include "MetricScale.h"

#include <QImage>
#include <QPainter>

#include <limits>

TEST(MetricScaleTest, ConvertsPhysicalDpiToDeviceIndependentPixelsPerMillimeter)
{
    EXPECT_NEAR(
        famp::metric::deviceIndependentPixelsPerMillimeter(109.22),
        4.3,
        1e-12);
    EXPECT_NEAR(
        famp::metric::deviceIndependentPixelsPerMillimeter(96.0),
        96.0 / 25.4,
        1e-12);
}

TEST(MetricScaleTest, ReproducesMapScaleFactorsFromPhysicalMillimeterSpacing)
{
    constexpr double pixelsPerMillimeter = 4.3;

    EXPECT_NEAR(famp::metric::pixelsPerMeterAtScale(pixelsPerMillimeter, 10),
                430.0, 1e-12);
    EXPECT_NEAR(famp::metric::pixelsPerMeterAtScale(pixelsPerMillimeter, 20),
                215.0, 1e-12);
    EXPECT_NEAR(famp::metric::pixelsPerMeterAtScale(pixelsPerMillimeter, 50),
                86.0, 1e-12);
    EXPECT_NEAR(famp::metric::pixelsPerMeterAtScale(pixelsPerMillimeter, 100),
                43.0, 1e-12);
}

TEST(MetricScaleTest, RejectsInvalidInputs)
{
    EXPECT_DOUBLE_EQ(
        famp::metric::deviceIndependentPixelsPerMillimeter(0.0), 0.0);
    EXPECT_DOUBLE_EQ(
        famp::metric::deviceIndependentPixelsPerMillimeter(-96.0), 0.0);
    EXPECT_DOUBLE_EQ(
        famp::metric::pixelsPerMeterAtScale(4.0, 0), 0.0);
    EXPECT_DOUBLE_EQ(
        famp::metric::pixelsPerMeterAtScale(0.0, 50), 0.0);
}

TEST(MetricScaleTest, FallsBackWhenPhysicalDpiIsUnavailable)
{
    EXPECT_DOUBLE_EQ(
        famp::metric::bestAvailableDotsPerInch(0.0, 120.0), 120.0);
    EXPECT_DOUBLE_EQ(
        famp::metric::bestAvailableDotsPerInch(
            std::numeric_limits<double>::quiet_NaN(), 0.0),
        famp::metric::DefaultDotsPerInch);
}

TEST(MetricScaleTest, CorrectsAOneHundredMillimeterCalibrationTarget)
{
    const double adjustment = famp::metric::calibrationAdjustment(
        famp::metric::CalibrationReferenceMillimeters, 96.0);
    EXPECT_NEAR(adjustment, 100.0 / 96.0, 1e-12);

    const double calibrated = famp::metric::calibratedPixelsPerMillimeter(
        3.6, adjustment);
    EXPECT_NEAR(calibrated * 96.0, 360.0, 1e-12);
    EXPECT_NEAR(calibrated * 100.0, 375.0, 1e-12);
}

TEST(MetricScaleTest, RejectsUnsafeCalibrationValues)
{
    EXPECT_DOUBLE_EQ(famp::metric::calibrationAdjustment(100.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(
        famp::metric::calibrationAdjustment(
            100.0, std::numeric_limits<double>::quiet_NaN()),
        0.0);
    EXPECT_FALSE(famp::metric::isValidCalibrationFactor(0.1));
    EXPECT_FALSE(famp::metric::isValidCalibrationFactor(5.1));
    EXPECT_DOUBLE_EQ(
        famp::metric::calibratedPixelsPerMillimeter(3.6, 0.1), 0.0);
}

TEST(MetricGridTest, RejectsUnrepresentableGridRangesWithoutLooping)
{
    QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);

    EXPECT_NO_FATAL_FAILURE(famp::metricgrid::draw(
        painter,
        QRectF(-1.0e300, -1.0e300, 2.0e300, 2.0e300),
        QPointF(1.0e-300, 1.0e-300)));
}
