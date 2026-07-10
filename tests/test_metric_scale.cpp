#include <gtest/gtest.h>

#include "MetricScale.h"

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
