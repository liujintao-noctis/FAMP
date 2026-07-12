#include <gtest/gtest.h>

#include "GraphicsUndoCommands.h"

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QUndoStack>

namespace
{
class TrackedRectItem final : public QGraphicsRectItem
{
public:
    explicit TrackedRectItem(bool* destroyed)
        : destroyed_(destroyed)
    {
    }

    ~TrackedRectItem() override
    {
        *destroyed_ = true;
    }

private:
    bool* destroyed_;
};
}

TEST(GraphicsUndoCommandsTest, RestoresAndReappliesItemTransform)
{
    QGraphicsScene scene;
    auto* item = scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0));
    auto handle = std::make_shared<famp::graphics::ItemLifetime>(item);
    QUndoStack history;

    const auto before = famp::graphics::captureItemStates({handle});
    item->setPos(12.0, -4.0);
    item->setRotation(35.0);
    item->setZValue(3.0);
    const auto after = famp::graphics::captureItemStates({handle});
    ASSERT_FALSE(famp::graphics::itemStatesEqual(before, after));

    history.push(famp::graphics::makeTransformCommand(
        before, after, QStringLiteral("transform")));
    history.undo();
    EXPECT_EQ(item->pos(), QPointF());
    EXPECT_DOUBLE_EQ(item->rotation(), 0.0);
    EXPECT_DOUBLE_EQ(item->zValue(), 0.0);

    history.redo();
    EXPECT_EQ(item->pos(), QPointF(12.0, -4.0));
    EXPECT_DOUBLE_EQ(item->rotation(), 35.0);
    EXPECT_DOUBLE_EQ(item->zValue(), 3.0);

    history.clear();
    handle.reset();
}

TEST(GraphicsUndoCommandsTest, AddsAndRemovesItemWithoutLosingIt)
{
    QGraphicsScene scene;
    auto* item = new QGraphicsRectItem(QRectF(0.0, 0.0, 5.0, 5.0));
    item->setFlag(QGraphicsItem::ItemIsSelectable);
    auto handle = std::make_shared<famp::graphics::ItemLifetime>(item);
    QUndoStack history;

    history.push(famp::graphics::makeAddItemCommand(
        &scene, handle, QStringLiteral("add")));
    EXPECT_EQ(item->scene(), &scene);

    history.undo();
    EXPECT_EQ(item->scene(), nullptr);
    history.redo();
    EXPECT_EQ(item->scene(), &scene);
    EXPECT_TRUE(item->isSelected());

    history.push(famp::graphics::makeRemoveItemsCommand(
        &scene, {handle}, QStringLiteral("remove")));
    EXPECT_EQ(item->scene(), nullptr);
    history.undo();
    EXPECT_EQ(item->scene(), &scene);
    history.redo();
    EXPECT_EQ(item->scene(), nullptr);
    history.undo();
    EXPECT_EQ(item->scene(), &scene);

    history.clear();
    handle.reset();
}

TEST(GraphicsUndoCommandsTest, RemovesOnlySelectedParentWhenChildIsIncluded)
{
    QGraphicsScene scene;
    auto* parent = scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0));
    auto* child = new QGraphicsRectItem(QRectF(0.0, 0.0, 2.0, 2.0), parent);
    auto parentHandle = std::make_shared<famp::graphics::ItemLifetime>(parent);
    auto childHandle = std::make_shared<famp::graphics::ItemLifetime>(child);
    QUndoStack history;

    history.push(famp::graphics::makeRemoveItemsCommand(
        &scene, {parentHandle, childHandle}, QStringLiteral("remove tree")));
    EXPECT_EQ(parent->scene(), nullptr);
    EXPECT_EQ(child->scene(), nullptr);

    history.undo();
    EXPECT_EQ(parent->scene(), &scene);
    EXPECT_EQ(child->scene(), &scene);
    EXPECT_EQ(child->parentItem(), parent);

    history.clear();
    childHandle.reset();
    parentHandle.reset();
}

TEST(GraphicsUndoCommandsTest, RestoresTextFont)
{
    QGraphicsScene scene;
    auto* item = scene.addText(QStringLiteral("text"));
    auto handle = std::make_shared<famp::graphics::ItemLifetime>(item);
    QUndoStack history;

    const QFont before = item->font();
    QFont after = before;
    after.setPointSize(before.pointSize() + 5);
    item->setFont(after);
    history.push(famp::graphics::makeTextFontCommand(
        handle, before, after, QStringLiteral("font")));

    history.undo();
    EXPECT_EQ(item->font(), before);
    history.redo();
    EXPECT_EQ(item->font(), after);

    history.clear();
    handle.reset();
}

TEST(GraphicsUndoCommandsTest, ReleasesUndoneAddedItemWhenHistoryIsDiscarded)
{
    QGraphicsScene scene;
    QUndoStack history;
    bool destroyed = false;
    auto handle = std::make_shared<famp::graphics::ItemLifetime>(
        new TrackedRectItem(&destroyed));

    history.push(famp::graphics::makeAddItemCommand(
        &scene, handle, QStringLiteral("add")));
    history.undo();
    history.clear();
    handle.reset();

    EXPECT_TRUE(destroyed);
}

TEST(GraphicsUndoCommandsTest, ReleasesRemovedItemWhenHistoryIsDiscarded)
{
    QGraphicsScene scene;
    QUndoStack history;
    bool destroyed = false;
    auto* item = new TrackedRectItem(&destroyed);
    scene.addItem(item);
    auto handle = std::make_shared<famp::graphics::ItemLifetime>(item);

    history.push(famp::graphics::makeRemoveItemsCommand(
        &scene, {handle}, QStringLiteral("remove")));
    history.clear();
    handle.reset();

    EXPECT_TRUE(destroyed);
}
