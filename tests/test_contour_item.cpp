#include "ContourItem.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
QVector<famp::terrain::ContourLine> worldLines()
{
    return {
        {10.0, {{{500000.0, 3400000.0},
                  {500001.0, 3400001.0},
                  {500002.0, 3400000.0}}}},
        {15.0, {{{500000.0, 3400002.0},
                  {500002.0, 3400002.0}}}}};
}
}

TEST(ContourItemTest, NormalizesLargeWorldCoordinatesAndRestoresThem)
{
    ContourItemData data;
    QString error;
    const auto input = worldLines();
    ASSERT_TRUE(ContourItem::createDataFromAbsolute(
        input, 1.0, QStringLiteral("EPSG:4547"),
        QStringLiteral("layer-1"), QStringLiteral("探方"),
        QStringLiteral("terrain.famp-dem"), 1.0, 0.0,
        data, &error)) << error.toStdString();
    EXPECT_DOUBLE_EQ(data.originX, 500000.0);
    EXPECT_DOUBLE_EQ(data.originY, 3400000.0);
    EXPECT_DOUBLE_EQ(data.relativeLines.front().points.front()[0], 0.0);
    EXPECT_DOUBLE_EQ(data.relativeLines.front().points.front()[1], 0.0);

    ContourItem item(data, QPointF(10.0, 20.0));
    EXPECT_EQ(item.pointCount(), 5U);
    EXPECT_LT(item.boundingRect().width(), 30.0);
    EXPECT_LT(item.boundingRect().height(), 50.0);
    const auto restored = item.absoluteLines();
    ASSERT_EQ(restored.size(), input.size());
    EXPECT_DOUBLE_EQ(restored.at(0).points.at(2)[0], 500002.0);
    EXPECT_DOUBLE_EQ(restored.at(1).points.at(0)[1], 3400002.0);
}

TEST(ContourItemTest, RescalesGeometryWithoutChangingSurveyCoordinates)
{
    ContourItemData data;
    QString error;
    ASSERT_TRUE(ContourItem::createDataFromAbsolute(
        worldLines(), 1.0, {}, {}, {}, {}, 1.0, 0.0, data, &error));
    ContourItem item(data, QPointF(10.0, 10.0));
    const QRectF before = item.boundingRect();
    const auto coordinates = item.absoluteLines();
    item.setSceneUnitsPerMeter(QPointF(20.0, 20.0));
    EXPECT_GT(item.boundingRect().width(), before.width());
    EXPECT_GT(item.boundingRect().height(), before.height());
    EXPECT_EQ(item.absoluteLines().size(), coordinates.size());
    EXPECT_DOUBLE_EQ(item.absoluteLines().front().points.back()[0],
                     coordinates.front().points.back()[0]);
}

TEST(ContourItemTest, RejectsInvalidOrOversizedInputAtomically)
{
    ContourItemData output;
    output.originX = 123.0;
    auto invalid = worldLines();
    invalid.front().points.front()[0] =
        std::numeric_limits<double>::infinity();
    QString error;
    EXPECT_FALSE(ContourItem::createDataFromAbsolute(
        invalid, 1.0, {}, {}, {}, {}, 1.0, 0.0, output, &error));
    EXPECT_DOUBLE_EQ(output.originX, 123.0);
    EXPECT_FALSE(error.isEmpty());
}
