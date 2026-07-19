#include "EntityTreeModel.h"

#include <gtest/gtest.h>

#include <QMimeData>
#include <QScopedPointer>

namespace
{

using famp::presentation::EntityTreeModel;
using famp::workspace::EntityKind;
using famp::workspace::WorkspaceStore;

} // namespace

TEST(EntityTreeModelTest, ExposesProjectRootAndTypedChildren)
{
    WorkspaceStore store;
    EntityTreeModel model(&store);
    ASSERT_EQ(model.rowCount(), 1);
    const QModelIndex root = model.index(0, 0);
    ASSERT_TRUE(root.isValid());
    EXPECT_EQ(model.data(root, EntityTreeModel::EntityKindRole).toInt(),
              static_cast<int>(EntityKind::ProjectRoot));

    const auto cloud = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud,
                                    QStringLiteral("大墓坑")));
    ASSERT_FALSE(cloud.isNull());
    ASSERT_EQ(model.rowCount(root), 1);
    const QModelIndex cloudIndex = model.index(0, 0, root);
    EXPECT_EQ(model.entityId(cloudIndex), cloud);
    EXPECT_EQ(model.data(cloudIndex, Qt::DisplayRole).toString(),
              QStringLiteral("大墓坑"));
    EXPECT_TRUE(model.flags(cloudIndex).testFlag(Qt::ItemIsDragEnabled));
}

TEST(EntityTreeModelTest, EditsUniquelyAndAggregatesVisibility)
{
    WorkspaceStore store;
    EntityTreeModel model(&store);
    const auto group = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("group")));
    const auto first = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("one")),
        group);
    const auto second = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("two")),
        group);

    const QModelIndex groupIndex = model.indexForId(group);
    const QModelIndex firstIndex = model.indexForId(first);
    const QModelIndex secondIndex = model.indexForId(second);
    ASSERT_TRUE(model.setData(secondIndex, QStringLiteral("one"), Qt::EditRole));
    EXPECT_EQ(store.entity(second)->name, QStringLiteral("one.1"));

    ASSERT_TRUE(model.setData(firstIndex, Qt::Unchecked, Qt::CheckStateRole));
    EXPECT_EQ(model.data(groupIndex, Qt::CheckStateRole).toInt(),
              static_cast<int>(Qt::PartiallyChecked));
    ASSERT_TRUE(model.setData(groupIndex, Qt::Unchecked, Qt::CheckStateRole));
    EXPECT_FALSE(store.entity(first)->visible);
    EXPECT_FALSE(store.entity(second)->visible);
    EXPECT_EQ(model.data(groupIndex, Qt::CheckStateRole).toInt(),
              static_cast<int>(Qt::Unchecked));
}

TEST(EntityTreeModelTest, MovesEntitiesWithInternalDragAndDrop)
{
    WorkspaceStore store;
    EntityTreeModel model(&store);
    const auto groupA = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("A")));
    const auto groupB = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("B")));
    const auto cloud = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("cloud")),
        groupA);

    QScopedPointer<QMimeData> mime(model.mimeData({model.indexForId(cloud)}));
    ASSERT_TRUE(mime);
    ASSERT_TRUE(model.canDropMimeData(mime.data(), Qt::MoveAction, -1, 0,
                                      model.indexForId(groupB)));
    ASSERT_TRUE(model.dropMimeData(mime.data(), Qt::MoveAction, -1, 0,
                                   model.indexForId(groupB)));
    EXPECT_EQ(store.entity(cloud)->parentId, groupB);
    EXPECT_EQ(model.parent(model.indexForId(cloud)), model.indexForId(groupB));
}

TEST(EntityTreeModelTest, ReordersSameParentUsingQtDropRowSemantics)
{
    WorkspaceStore store;
    EntityTreeModel model(&store);
    const auto group = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("group")));
    const auto first = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("a")),
        group);
    const auto second = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("b")),
        group);
    const auto third = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("c")),
        group);
    const auto fourth = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("d")),
        group);

    QScopedPointer<QMimeData> mime(model.mimeData({model.indexForId(second)}));
    ASSERT_TRUE(model.dropMimeData(
        mime.data(), Qt::MoveAction, 3, 0, model.indexForId(group)));
    EXPECT_EQ(store.children(group),
              QVector<famp::workspace::EntityId>(
                  {first, third, second, fourth}));
}

TEST(EntityTreeModelTest, MultiItemDropFailureDoesNotMoveAnyEntity)
{
    WorkspaceStore store;
    EntityTreeModel model(&store);
    const auto source = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("source")));
    const auto target = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("target")));
    const auto first = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("a")),
        source);
    const auto locked = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("b")),
        source);
    ASSERT_TRUE(store.setLocked(locked, true));

    QScopedPointer<QMimeData> mime(model.mimeData(
        {model.indexForId(first), model.indexForId(locked)}));
    EXPECT_FALSE(model.dropMimeData(
        mime.data(), Qt::MoveAction, -1, 0, model.indexForId(target)));
    EXPECT_EQ(store.children(source),
              QVector<famp::workspace::EntityId>({first, locked}));
    EXPECT_TRUE(store.children(target).isEmpty());
}

TEST(EntityTreeModelTest, RecursiveRemovalLeavesValidIndexes)
{
    WorkspaceStore store;
    EntityTreeModel model(&store);
    const auto group = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("group")));
    const auto cloud = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("cloud")),
        group);
    ASSERT_TRUE(model.indexForId(group).isValid());
    ASSERT_TRUE(model.indexForId(cloud).isValid());
    ASSERT_TRUE(store.removeEntity(group));
    EXPECT_FALSE(model.indexForId(group).isValid());
    EXPECT_FALSE(model.indexForId(cloud).isValid());
    EXPECT_EQ(model.rowCount(model.index(0, 0)), 0);
}

TEST(EntityTreeModelTest, LockedEntityCannotBeEditedOrDragged)
{
    WorkspaceStore store;
    EntityTreeModel model(&store);
    const auto cloud = store.addEntity(
        famp::workspace::makeEntity(EntityKind::PointCloud, QStringLiteral("cloud")));
    ASSERT_TRUE(store.setLocked(cloud, true));
    const QModelIndex cloudIndex = model.indexForId(cloud);
    EXPECT_FALSE(model.flags(cloudIndex).testFlag(Qt::ItemIsEditable));
    EXPECT_FALSE(model.flags(cloudIndex).testFlag(Qt::ItemIsDragEnabled));
    EXPECT_FALSE(model.setData(cloudIndex, QStringLiteral("renamed"), Qt::EditRole));
    EXPECT_EQ(store.entity(cloud)->name, QStringLiteral("cloud"));
}
