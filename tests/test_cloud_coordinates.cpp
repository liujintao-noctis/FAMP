#include <gtest/gtest.h>

#include "CloudCoordinates.h"

#include <limits>

TEST(CloudCoordinatesTest, RoundTripsOriginAndAffineTransform)
{
    famp::cloud::SpatialReference spatial;
    spatial.origin = {1000.0, 2000.0, 50.0};
    spatial.transform = {
        0.0, -1.0, 0.0, 500.0,
        1.0,  0.0, 0.0, -200.0,
        0.0,  0.0, 1.0, 10.0,
        0.0,  0.0, 0.0, 1.0};
    const famp::cloud::Point3d local{2.0, 3.0, 4.0};
    famp::cloud::Point3d real{};
    QString error;

    ASSERT_TRUE(famp::cloud::localToReal(
        spatial, local, real, &error)) << error.toStdString();
    EXPECT_DOUBLE_EQ(real[0], -1503.0);
    EXPECT_DOUBLE_EQ(real[1], 802.0);
    EXPECT_DOUBLE_EQ(real[2], 64.0);

    famp::cloud::Point3d restored{};
    ASSERT_TRUE(famp::cloud::realToLocal(
        spatial, real, restored, &error)) << error.toStdString();
    EXPECT_NEAR(restored[0], local[0], 1.0e-12);
    EXPECT_NEAR(restored[1], local[1], 1.0e-12);
    EXPECT_NEAR(restored[2], local[2], 1.0e-12);
}

TEST(CloudCoordinatesTest, RejectsInvalidAndSingularTransformsAtomically)
{
    famp::cloud::SpatialReference spatial;
    spatial.transform.fill(0.0);
    famp::cloud::Point3d output{7.0, 8.0, 9.0};
    QString error;

    EXPECT_FALSE(famp::cloud::realToLocal(
        spatial, {1.0, 2.0, 3.0}, output, &error));
    EXPECT_EQ(output, (famp::cloud::Point3d{7.0, 8.0, 9.0}));
    EXPECT_FALSE(error.isEmpty());

    spatial = famp::cloud::SpatialReference{};
    error.clear();
    EXPECT_FALSE(famp::cloud::localToReal(
        spatial,
        {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
        output,
        &error));
    EXPECT_EQ(output, (famp::cloud::Point3d{7.0, 8.0, 9.0}));
    EXPECT_FALSE(error.isEmpty());
}
