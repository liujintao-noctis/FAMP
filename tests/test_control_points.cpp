#include "ControlPoints.h"

#include <gtest/gtest.h>

#include <Eigen/Geometry>

#include <cmath>
#include <limits>

namespace
{
famp::control::Point point(
    const QString& name,
    const famp::cloud::Point3d& local,
    const famp::cloud::Point3d& target,
    bool enabled = true)
{
    famp::control::Point result;
    result.id = famp::control::createPointId();
    result.name = name;
    result.local = local;
    result.target = target;
    result.enabled = enabled;
    return result;
}

famp::cloud::Point3d transformed(
    const Eigen::Matrix3d& rotation,
    const Eigen::Vector3d& translation,
    const famp::cloud::Point3d& value)
{
    const Eigen::Vector3d output = rotation * Eigen::Vector3d(
        value[0], value[1], value[2]) + translation;
    return {output.x(), output.y(), output.z()};
}
}

TEST(ControlPointsTest, ValidatesIdsNamesCoordinatesAndUniqueness)
{
    QVector<famp::control::Point> points{
        point(QStringLiteral("CP-1"), {0.0, 0.0, 0.0}, {1.0, 2.0, 3.0}),
        point(QStringLiteral("CP-2"), {1.0, 0.0, 0.0}, {2.0, 2.0, 3.0})};
    QString error;
    EXPECT_TRUE(famp::control::validatePoints(points, &error))
        << error.toStdString();

    points[1].name = QStringLiteral(" cp-1 ");
    EXPECT_FALSE(famp::control::validatePoints(points, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("名称")));

    points[1].name = QStringLiteral("CP-2");
    points[1].target[2] = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(famp::control::validatePoints(points, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("坐标")));
}

TEST(ControlPointsTest, SolvesRigidTransformAndReportsNearZeroResiduals)
{
    const famp::cloud::Point3d origin{1000.0, 2000.0, 50.0};
    const Eigen::Matrix3d rotation =
        Eigen::AngleAxisd(0.35, Eigen::Vector3d::UnitZ()).toRotationMatrix()
        * Eigen::AngleAxisd(-0.18, Eigen::Vector3d::UnitX()).toRotationMatrix();
    const Eigen::Vector3d translation(500.0, -240.0, 12.0);
    const QVector<famp::cloud::Point3d> locals{
        {0.0, 0.0, 0.0},
        {4.0, 0.0, 0.0},
        {0.0, 3.0, 0.0},
        {2.0, 1.0, 5.0},
        {-1.0, 2.0, 1.0}};
    QVector<famp::control::Point> points;
    for (int index = 0; index < locals.size(); ++index)
    {
        const famp::cloud::Point3d absolute{
            locals[index][0] + origin[0],
            locals[index][1] + origin[1],
            locals[index][2] + origin[2]};
        points.append(point(
            QStringLiteral("CP-%1").arg(index + 1), locals[index],
            transformed(rotation, translation, absolute)));
    }

    famp::control::Solution solution;
    QString error;
    ASSERT_TRUE(famp::control::solveRigid(
        points, solution, &error)) << error.toStdString();
    EXPECT_EQ(solution.quality.enabledPointCount, points.size());
    EXPECT_NEAR(solution.quality.rootMeanSquare, 0.0, 1.0e-9);
    EXPECT_NEAR(solution.quality.maximum, 0.0, 1.0e-9);

    for (const auto& controlPoint : points)
    {
        famp::cloud::Point3d real;
        ASSERT_TRUE(famp::cloud::localToReal(
            solution.spatial, controlPoint.local, real, &error));
        for (std::size_t axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(real[axis], controlPoint.target[axis], 1.0e-9);
    }
}

TEST(ControlPointsTest, KeepsPrecisionWithMillimeterLocalCoordinates)
{
    const Eigen::Matrix3d rotation =
        Eigen::AngleAxisd(0.2, Eigen::Vector3d::UnitZ()).toRotationMatrix();
    const Eigen::Vector3d targetOrigin(500000.0, 3400000.0, 25.0);
    const QVector<famp::cloud::Point3d> locals{
        {0.0, 0.0, 0.0},
        {0.004, 0.0, 0.0},
        {0.0, 0.003, 0.0},
        {0.001, 0.002, 0.005}};
    QVector<famp::control::Point> points;
    for (int index = 0; index < locals.size(); ++index)
    {
        points.append(point(
            QStringLiteral("P-%1").arg(index + 1), locals[index],
            transformed(rotation, targetOrigin, locals[index])));
    }

    famp::control::Solution solution;
    QString error;
    ASSERT_TRUE(famp::control::solveRigid(
        points, solution, &error)) << error.toStdString();
    EXPECT_LT(solution.quality.rootMeanSquare, 1.0e-8);
    for (const auto& controlPoint : points)
    {
        famp::cloud::Point3d real;
        ASSERT_TRUE(famp::cloud::localToReal(
            solution.spatial, controlPoint.local, real, &error));
        for (std::size_t axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(real[axis], controlPoint.target[axis], 1.0e-7);
    }
}

TEST(ControlPointsTest, IgnoresDisabledPointsDuringSolveAndQualityEvaluation)
{
    QVector<famp::control::Point> points{
        point(QStringLiteral("A"), {0.0, 0.0, 0.0}, {10.0, 20.0, 30.0}),
        point(QStringLiteral("B"), {1.0, 0.0, 0.0}, {11.0, 20.0, 30.0}),
        point(QStringLiteral("C"), {0.0, 1.0, 0.0}, {10.0, 21.0, 30.0}),
        point(QStringLiteral("outlier"), {0.0, 0.0, 1.0},
              {999.0, 999.0, 999.0}, false)};
    famp::control::Solution solution;
    QString error;
    ASSERT_TRUE(famp::control::solveRigid(
        points, solution, &error));
    EXPECT_EQ(solution.quality.enabledPointCount, 3);
    EXPECT_EQ(solution.quality.residuals.size(), 3);
    EXPECT_NEAR(solution.quality.rootMeanSquare, 0.0, 1.0e-10);
}

TEST(ControlPointsTest, ComputesResidualSummaryInCoordinateUnits)
{
    const QVector<famp::control::Point> points{
        point(QStringLiteral("A"), {0.0, 0.0, 0.0}, {3.0, 0.0, 0.0}),
        point(QStringLiteral("B"), {0.0, 0.0, 0.0}, {0.0, 4.0, 0.0})};
    famp::control::Quality quality;
    QString error;
    ASSERT_TRUE(famp::control::evaluate(
        points, {}, quality, &error)) << error.toStdString();
    EXPECT_EQ(quality.enabledPointCount, 2);
    EXPECT_NEAR(quality.rootMeanSquare, std::sqrt(12.5), 1.0e-12);
    EXPECT_NEAR(quality.mean, 3.5, 1.0e-12);
    EXPECT_NEAR(quality.maximum, 4.0, 1.0e-12);
}

TEST(ControlPointsTest, RejectsInsufficientOrCollinearGeometryAtomically)
{
    QVector<famp::control::Point> points{
        point(QStringLiteral("A"), {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
        point(QStringLiteral("B"), {1.0, 0.0, 0.0}, {11.0, 0.0, 0.0}),
        point(QStringLiteral("C"), {2.0, 0.0, 0.0}, {12.0, 0.0, 0.0})};
    famp::control::Solution output;
    output.spatial.origin = {7.0, 8.0, 9.0};
    QString error;
    EXPECT_FALSE(famp::control::solveRigid(
        points, output, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("共线")));
    EXPECT_DOUBLE_EQ(output.spatial.origin[0], 7.0);

    points[2].enabled = false;
    EXPECT_FALSE(famp::control::solveRigid(
        points, output, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("至少")));
}

TEST(ControlPointsTest, RebasesCenteredDisplayCoordinatesWithoutTranslation)
{
    famp::cloud::SpatialReference before;
    famp::cloud::SpatialReference after;
    const Eigen::Matrix3d rotation =
        Eigen::AngleAxisd(0.5, Eigen::Vector3d::UnitZ()).toRotationMatrix();
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            after.transform[static_cast<std::size_t>(row * 4 + column)] =
                rotation(row, column);
        }
    }
    after.transform[3] = 500000.0;
    after.transform[7] = -3400000.0;
    after.transform[11] = 12.0;

    const famp::cloud::Point3d input{3.0, 4.0, 5.0};
    famp::cloud::Point3d rotated;
    QString error;
    ASSERT_TRUE(famp::control::transformDisplayPoint(
        before, after, input, rotated, &error)) << error.toStdString();
    const Eigen::Vector3d expected = rotation * Eigen::Vector3d(3.0, 4.0, 5.0);
    EXPECT_NEAR(rotated[0], expected.x(), 1.0e-12);
    EXPECT_NEAR(rotated[1], expected.y(), 1.0e-12);
    EXPECT_NEAR(rotated[2], expected.z(), 1.0e-12);
    EXPECT_NEAR(std::hypot(rotated[0], rotated[1]), 5.0, 1.0e-12);

    famp::cloud::Point3d restored;
    ASSERT_TRUE(famp::control::transformDisplayPoint(
        after, before, rotated, restored, &error));
    for (std::size_t axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(restored[axis], input[axis], 1.0e-12);
}
