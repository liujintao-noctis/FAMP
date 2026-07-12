#include <gtest/gtest.h>

#include "CompassItem.h"
#include "FormTabulationItem.h"
#include "GraphicsSceneDocument.h"
#include "MeasurementItem.h"
#include "MyItem.h"

#include <QGraphicsItemGroup>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QJsonArray>
#include <QJsonObject>

TEST(GraphicsSceneDocumentTest, RoundTripsSupportedItemsAndTransforms)
{
    QGraphicsScene source(QRectF(-400.0, -300.0, 800.0, 600.0));

    QVector<QPointF> projectedPoints{
        QPointF(-10.0, 5.0), QPointF(20.0, 30.0), QPointF(50.0, 10.0)};
    auto* projection = new MyItem(projectedPoints, XOY);
    projection->setData(1, QStringLiteral("探方边界"));
    projection->setPos(12.0, -8.0);
    projection->setRotation(15.0);
    projection->setZValue(3.0);
    source.addItem(projection);

    auto* measurement = new MeasurementItem(
        famp::measurement::Kind::Area,
        {QPointF(0.0, 0.0), QPointF(3.0, 0.0), QPointF(3.0, 4.0)},
        QPointF(10.0, 12.0));
    source.addItem(measurement);

    auto* table = new FormTabulationItem(
        QStringLiteral("李四"), QStringLiteral("2026-07-12"),
        QStringLiteral("1:50"), nullptr);
    table->setOpacity(0.8);
    source.addItem(table);

    auto* group = new QGraphicsItemGroup();
    auto* compass = new CompassItem();
    auto* text = new QGraphicsTextItem(QStringLiteral("遗迹 F12"));
    text->setDefaultTextColor(QColor(10, 20, 30, 200));
    text->setPos(5.0, 9.0);
    group->addToGroup(compass);
    group->addToGroup(text);
    group->setPos(-25.0, 40.0);
    source.addItem(group);

    QString error;
    const QJsonObject document = famp::graphicsdoc::saveScene(&source, &error);
    ASSERT_FALSE(document.isEmpty()) << error.toStdString();
    EXPECT_EQ(document.value(QStringLiteral("schemaVersion")).toInt(),
              famp::graphicsdoc::SchemaVersion);
    EXPECT_EQ(document.value(QStringLiteral("items")).toArray().size(), 4);

    QGraphicsScene restored;
    QList<QGraphicsItem*> restoredRoots;
    ASSERT_TRUE(famp::graphicsdoc::restoreScene(
        &restored, document, &restoredRoots, &error)) << error.toStdString();
    EXPECT_EQ(restoredRoots.size(), 4);
    EXPECT_EQ(restored.sceneRect(), source.sceneRect());

    MyItem* restoredProjection = nullptr;
    MeasurementItem* restoredMeasurement = nullptr;
    FormTabulationItem* restoredTable = nullptr;
    QGraphicsItemGroup* restoredGroup = nullptr;
    for (QGraphicsItem* item : restoredRoots)
    {
        if (auto* candidate = dynamic_cast<MyItem*>(item))
            restoredProjection = candidate;
        else if (auto* candidate = dynamic_cast<MeasurementItem*>(item))
            restoredMeasurement = candidate;
        else if (auto* candidate = dynamic_cast<FormTabulationItem*>(item))
            restoredTable = candidate;
        else if (auto* candidate = dynamic_cast<QGraphicsItemGroup*>(item))
            restoredGroup = candidate;
    }

    ASSERT_NE(restoredProjection, nullptr);
    EXPECT_EQ(restoredProjection->points(), projectedPoints);
    EXPECT_EQ(restoredProjection->projectionType(), XOY);
    EXPECT_EQ(restoredProjection->data(1).toString(), QStringLiteral("探方边界"));
    EXPECT_EQ(restoredProjection->pos(), QPointF(12.0, -8.0));
    EXPECT_DOUBLE_EQ(restoredProjection->rotation(), 15.0);
    EXPECT_DOUBLE_EQ(restoredProjection->zValue(), 3.0);

    ASSERT_NE(restoredMeasurement, nullptr);
    EXPECT_EQ(restoredMeasurement->kind(), famp::measurement::Kind::Area);
    EXPECT_DOUBLE_EQ(restoredMeasurement->value(), 6.0);
    EXPECT_EQ(restoredMeasurement->sceneUnitsPerMeter(), QPointF(10.0, 12.0));

    ASSERT_NE(restoredTable, nullptr);
    EXPECT_EQ(restoredTable->designerText(), QStringLiteral("李四"));
    EXPECT_EQ(restoredTable->dateText(), QStringLiteral("2026-07-12"));
    EXPECT_EQ(restoredTable->scaleText(), QStringLiteral("1:50"));
    EXPECT_DOUBLE_EQ(restoredTable->opacity(), 0.8);

    ASSERT_NE(restoredGroup, nullptr);
    EXPECT_EQ(restoredGroup->childItems().size(), 2);
    EXPECT_EQ(restoredGroup->pos(), QPointF(-25.0, 40.0));
}

TEST(GraphicsSceneDocumentTest, RejectsInvalidItemWithoutMutatingScene)
{
    QGraphicsScene source(QRectF(-100.0, -100.0, 200.0, 200.0));
    source.addText(QStringLiteral("new content"));
    QString error;
    QJsonObject document = famp::graphicsdoc::saveScene(&source, &error);
    ASSERT_FALSE(document.isEmpty()) << error.toStdString();

    QJsonArray items = document.value(QStringLiteral("items")).toArray();
    QJsonObject item = items.at(0).toObject();
    QJsonObject state = item.value(QStringLiteral("state")).toObject();
    state.insert(QStringLiteral("scale"), -1.0);
    item.insert(QStringLiteral("state"), state);
    items.replace(0, item);
    document.insert(QStringLiteral("items"), items);

    QGraphicsScene target;
    auto* preserved = target.addText(QStringLiteral("keep me"));
    EXPECT_FALSE(famp::graphicsdoc::restoreScene(
        &target, document, nullptr, &error));
    EXPECT_FALSE(error.isEmpty());
    ASSERT_EQ(target.items().size(), 1);
    EXPECT_EQ(target.items().front(), preserved);
    EXPECT_EQ(preserved->toPlainText(), QStringLiteral("keep me"));
}
