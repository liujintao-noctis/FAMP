#include <gtest/gtest.h>

#include <QGraphicsRectItem>

#include "GraphicsItemTransform.h"

#include <limits>

TEST(GraphicsItemTransformTest, RotatesEveryValidItemAroundItsCenter)
{
    QGraphicsRectItem first(QRectF(10.0, 20.0, 40.0, 20.0));
    QGraphicsRectItem second(QRectF(-5.0, -5.0, 10.0, 10.0));
    QList<QGraphicsItem*> items{&first, nullptr, &second};

    EXPECT_EQ(famp::graphics::rotateItems(items, 5.0), 2);
    EXPECT_DOUBLE_EQ(first.rotation(), 5.0);
    EXPECT_EQ(first.transformOriginPoint(), QPointF(30.0, 30.0));
    EXPECT_DOUBLE_EQ(second.rotation(), 5.0);
    EXPECT_EQ(second.transformOriginPoint(), QPointF(0.0, 0.0));
}

TEST(GraphicsItemTransformTest, NormalizesAccumulatedRotation)
{
    QGraphicsRectItem item(QRectF(0.0, 0.0, 10.0, 10.0));
    item.setRotation(179.0);

    EXPECT_EQ(famp::graphics::rotateItems({&item}, 5.0), 1);
    EXPECT_DOUBLE_EQ(item.rotation(), -176.0);
    EXPECT_DOUBLE_EQ(famp::graphics::normalizedRotation(-181.0), 179.0);
}

TEST(GraphicsItemTransformTest, RejectsInvalidOrZeroDelta)
{
    QGraphicsRectItem item(QRectF(0.0, 0.0, 10.0, 10.0));

    EXPECT_EQ(famp::graphics::rotateItems({&item}, 0.0), 0);
    EXPECT_EQ(famp::graphics::rotateItems(
                  {&item}, std::numeric_limits<qreal>::infinity()),
              0);
    EXPECT_DOUBLE_EQ(item.rotation(), 0.0);
}
