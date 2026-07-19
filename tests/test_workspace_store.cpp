#include "EntityWriterRegistry.h"
#include "RendererRegistry.h"
#include "WorkspaceStore.h"

#include <gtest/gtest.h>

namespace
{

using famp::workspace::EntityKind;
using famp::workspace::WorkspaceEntity;
using famp::workspace::WorkspaceStore;

WorkspaceEntity cloudEntity(const QString& name)
{
    WorkspaceEntity entity = famp::workspace::makeEntity(EntityKind::PointCloud, name);
    entity.setPayload(std::make_shared<int>(42));
    return entity;
}

} // namespace

TEST(WorkspaceStoreTest, CreatesStableRootAndUniqueSiblingNames)
{
    WorkspaceStore store;
    ASSERT_FALSE(store.rootId().isNull());
    ASSERT_NE(store.entity(store.rootId()), nullptr);
    EXPECT_EQ(store.entity(store.rootId())->kind, EntityKind::ProjectRoot);
    EXPECT_EQ(store.size(), 1);

    const auto first = store.addEntity(cloudEntity(QStringLiteral("大墓坑")));
    const auto second = store.addEntity(cloudEntity(QStringLiteral("大墓坑")));
    ASSERT_FALSE(first.isNull());
    ASSERT_FALSE(second.isNull());
    EXPECT_EQ(store.entity(first)->name, QStringLiteral("大墓坑"));
    EXPECT_EQ(store.entity(second)->name, QStringLiteral("大墓坑.1"));
    EXPECT_EQ(store.children(store.rootId()),
              QVector<famp::workspace::EntityId>({first, second}));
}

TEST(WorkspaceStoreTest, KeepsSelfContainedDerivativeWhenSourceIsDeleted)
{
    WorkspaceStore store;
    const auto source = store.addEntity(cloudEntity(QStringLiteral("source")));
    ASSERT_FALSE(source.isNull());

    WorkspaceEntity derived = cloudEntity(QStringLiteral("source_voxel"));
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("voxel_downsample");
    provenance.parameters.insert(QStringLiteral("leafSize"), 0.05);
    derived.provenance = provenance;
    const auto result = store.addDerivedEntity(derived, source);
    ASSERT_FALSE(result.isNull());
    ASSERT_EQ(store.entity(result)->parentId, store.entity(source)->parentId);

    ASSERT_TRUE(store.removeEntity(source));
    ASSERT_NE(store.entity(result), nullptr);
    ASSERT_TRUE(store.entity(result)->payloadAs<int>());
    EXPECT_EQ(*store.entity(result)->payloadAs<int>(), 42);
    ASSERT_TRUE(store.entity(result)->provenance.has_value());
    EXPECT_TRUE(store.entity(result)->provenance->sourceIds.contains(source));
}

TEST(WorkspaceStoreTest, InsertsDerivedCloudImmediatelyAfterSourceBeforeUnrelatedGroups)
{
    WorkspaceStore store;
    const auto source = store.addEntity(cloudEntity(QStringLiteral("大墓坑")));
    const auto draftingGroup = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group,
                                    QStringLiteral("二维制图")));
    ASSERT_FALSE(source.isNull());
    ASSERT_FALSE(draftingGroup.isNull());

    WorkspaceEntity cropped = cloudEntity(QStringLiteral("大墓坑_裁切"));
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("crop");
    cropped.provenance = provenance;
    const auto result = store.addDerivedEntity(cropped, source);
    ASSERT_FALSE(result.isNull());

    EXPECT_EQ(store.entity(result)->parentId, store.entity(source)->parentId);
    EXPECT_EQ(store.rowOf(result), store.rowOf(source) + 1);
    EXPECT_EQ(store.children(store.rootId()),
              QVector<famp::workspace::EntityId>(
                  {source, result, draftingGroup}));
}

TEST(WorkspaceStoreTest, EnforcesLockAndCycleBoundariesAtomically)
{
    WorkspaceStore store;
    const auto groupA = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("A")));
    const auto groupB = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("B")),
        groupA);
    const auto cloud = store.addEntity(cloudEntity(QStringLiteral("locked")),
                                       groupB);
    ASSERT_TRUE(store.setLocked(cloud, true));

    QString error;
    EXPECT_FALSE(store.removeEntity(groupA, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(store.contains(groupA));
    EXPECT_TRUE(store.contains(groupB));
    EXPECT_TRUE(store.contains(cloud));

    EXPECT_FALSE(store.moveEntity(groupA, groupB, -1, &error));
    EXPECT_TRUE(store.contains(groupA));
    EXPECT_EQ(store.entity(groupB)->parentId, groupA);
}

TEST(WorkspaceStoreTest, MovesMultipleEntitiesAtomicallyAndResolvesNames)
{
    WorkspaceStore store;
    const auto source = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("source")));
    const auto target = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("target")));
    const auto first = store.addEntity(
        cloudEntity(QStringLiteral("same")), source);
    const auto second = store.addEntity(
        cloudEntity(QStringLiteral("second")), source);
    const auto existing = store.addEntity(
        cloudEntity(QStringLiteral("same")), target);
    ASSERT_TRUE(store.setLocked(second, true));

    QString error;
    EXPECT_FALSE(store.moveEntities({first, second}, target, -1, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(store.entity(first)->parentId, source);
    EXPECT_EQ(store.entity(second)->parentId, source);
    EXPECT_EQ(store.children(target),
              QVector<famp::workspace::EntityId>({existing}));

    ASSERT_TRUE(store.setLocked(second, false));
    ASSERT_TRUE(store.moveEntities({first, second}, target, 0, &error))
        << error.toStdString();
    EXPECT_EQ(store.children(source).size(), 0);
    EXPECT_EQ(store.children(target),
              QVector<famp::workspace::EntityId>({first, second, existing}));
    EXPECT_EQ(store.entity(first)->name, QStringLiteral("same.1"));
    EXPECT_EQ(store.entity(second)->name, QStringLiteral("second"));
}

TEST(WorkspaceStoreTest, PropagatesVisibilityAndReportsAggregateInputs)
{
    WorkspaceStore store;
    const auto group = store.addEntity(
        famp::workspace::makeEntity(EntityKind::Group, QStringLiteral("results")));
    const auto first = store.addEntity(cloudEntity(QStringLiteral("one")), group);
    const auto second = store.addEntity(cloudEntity(QStringLiteral("two")), group);
    ASSERT_TRUE(store.setVisible(group, false));
    EXPECT_FALSE(store.entity(group)->visible);
    EXPECT_FALSE(store.entity(first)->visible);
    EXPECT_FALSE(store.entity(second)->visible);

    ASSERT_TRUE(store.setVisible(first, true));
    EXPECT_TRUE(store.entity(first)->visible);
    EXPECT_FALSE(store.entity(second)->visible);
}

TEST(WorkspaceStoreTest, RejectsInvalidProvenanceWithoutMutation)
{
    WorkspaceStore store;
    WorkspaceEntity result = cloudEntity(QStringLiteral("bad"));
    famp::workspace::Provenance provenance;
    provenance.operation.clear();
    result.provenance = provenance;
    QString error;
    EXPECT_TRUE(store.addEntity(result, {}, -1, &error).isNull());
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(store.size(), 1);
}

TEST(WorkspaceStoreTest, ReplacesEntityWithoutBreakingStoreInvariants)
{
    WorkspaceStore store;
    const auto first = store.addEntity(cloudEntity(QStringLiteral("cloud")));
    const auto second = store.addEntity(cloudEntity(QStringLiteral("other")));
    ASSERT_FALSE(first.isNull());
    ASSERT_FALSE(second.isNull());

    WorkspaceEntity replacement = *store.entity(second);
    replacement.name = QStringLiteral("  cloud  ");
    replacement.assetPath = QStringLiteral("   ");
    QString error;
    ASSERT_TRUE(store.replaceEntity(replacement, &error))
        << error.toStdString();
    EXPECT_EQ(store.entity(second)->name, QStringLiteral("cloud.1"));
    EXPECT_FALSE(store.entity(second)->assetPath.has_value());

    const WorkspaceEntity accepted = *store.entity(second);
    WorkspaceEntity invalid = accepted;
    invalid.name = QString(256, QLatin1Char('x'));
    EXPECT_FALSE(store.replaceEntity(invalid, &error));
    EXPECT_EQ(store.entity(second)->name, accepted.name);

    invalid = accepted;
    invalid.assetPath = QString(QChar::Null);
    EXPECT_FALSE(store.replaceEntity(invalid, &error));
    EXPECT_EQ(store.entity(second)->assetPath, accepted.assetPath);
}

TEST(WorkspaceRegistryTest, DispatchesRendererAndWriterByEntityKind)
{
    WorkspaceEntity entity = cloudEntity(QStringLiteral("cloud"));
    int shown = 0;
    int hidden = 0;
    int removed = 0;
    famp::workspace::RendererRegistry renderers;
    famp::workspace::RendererCallbacks callbacks;
    callbacks.show = [&](const WorkspaceEntity&, QString*) {
        ++shown;
        return true;
    };
    callbacks.hide = [&](const WorkspaceEntity&) { ++hidden; };
    callbacks.remove = [&](const WorkspaceEntity&) { ++removed; };
    ASSERT_TRUE(renderers.registerRenderer(EntityKind::PointCloud, callbacks));
    ASSERT_TRUE(renderers.applyVisibility(entity));
    entity.visible = false;
    ASSERT_TRUE(renderers.applyVisibility(entity));
    renderers.remove(entity);
    EXPECT_EQ(shown, 1);
    EXPECT_EQ(hidden, 1);
    EXPECT_EQ(removed, 1);

    famp::workspace::EntityWriterRegistry writers;
    bool wrote = false;
    famp::workspace::EntityWriter writer;
    writer.description = QStringLiteral("PCD");
    writer.extensions = {QStringLiteral(".PCD"), QStringLiteral("pcd")};
    writer.write = [&](const WorkspaceEntity&, const QString& path, QString*) {
        wrote = path.endsWith(QStringLiteral(".pcd"));
        return wrote;
    };
    ASSERT_TRUE(writers.registerWriter(EntityKind::PointCloud, writer));
    EXPECT_EQ(writers.writer(EntityKind::PointCloud)->extensions,
              QStringList({QStringLiteral("pcd")}));
    EXPECT_TRUE(writers.write(entity, QStringLiteral("result.pcd")));
    EXPECT_TRUE(wrote);
}
