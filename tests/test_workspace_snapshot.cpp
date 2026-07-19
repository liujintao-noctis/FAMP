#include "WorkspaceSnapshot.h"

#include <gtest/gtest.h>

#include <QJsonArray>

namespace
{
famp::workspace::WorkspaceEntity cloudEntity(const QString& name)
{
    auto entity = famp::workspace::makeEntity(
        famp::workspace::EntityKind::PointCloud, name);
    entity.setPayload(std::make_shared<int>(7));
    return entity;
}
}

TEST(WorkspaceSnapshotTest, RoundTripsHierarchyAndProvenance)
{
    famp::workspace::WorkspaceStore source;
    ASSERT_TRUE(source.setName(source.rootId(), QStringLiteral("大墓坑项目")));
    const auto group = source.addEntity(famp::workspace::makeEntity(
        famp::workspace::EntityKind::Group, QStringLiteral("配准成果")));
    const auto input = source.addEntity(cloudEntity(QStringLiteral("source")), group);
    auto derived = cloudEntity(QStringLiteral("source_voxel"));
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("voxel_downsample");
    provenance.sourceIds = {input};
    provenance.parameters.insert(QStringLiteral("leafSize"), 0.05);
    derived.provenance = provenance;
    const auto result = source.addEntity(derived, group);
    ASSERT_FALSE(result.isNull());
    ASSERT_TRUE(source.setVisible(input, false));
    ASSERT_TRUE(source.setLocked(result, true));

    QString error;
    const QJsonObject json = famp::workspace::serializeSnapshot(source, &error);
    ASSERT_FALSE(json.isEmpty()) << error.toStdString();
    famp::workspace::WorkspaceSnapshot snapshot;
    ASSERT_TRUE(famp::workspace::deserializeSnapshot(json, snapshot, &error))
        << error.toStdString();

    famp::workspace::WorkspaceStore restored;
    auto restoredInput = cloudEntity(QStringLiteral("temporary"));
    restoredInput.id = input;
    ASSERT_FALSE(restored.addEntity(restoredInput).isNull());
    auto restoredResult = cloudEntity(QStringLiteral("temporary-result"));
    restoredResult.id = result;
    ASSERT_FALSE(restored.addEntity(restoredResult).isNull());
    ASSERT_TRUE(famp::workspace::applySnapshot(snapshot, restored, &error))
        << error.toStdString();

    EXPECT_EQ(restored.entity(restored.rootId())->name,
              QStringLiteral("大墓坑项目"));
    ASSERT_NE(restored.entity(group), nullptr);
    EXPECT_EQ(restored.entity(input)->parentId, group);
    EXPECT_FALSE(restored.entity(input)->visible);
    EXPECT_EQ(restored.entity(result)->parentId, group);
    EXPECT_TRUE(restored.entity(result)->locked);
    ASSERT_TRUE(restored.entity(result)->provenance.has_value());
    EXPECT_EQ(restored.entity(result)->provenance->operation,
              QStringLiteral("voxel_downsample"));
    ASSERT_TRUE(restored.entity(result)->payloadAs<int>());
    EXPECT_EQ(*restored.entity(result)->payloadAs<int>(), 7);
}

TEST(WorkspaceSnapshotTest, RejectsCyclesWithoutMutatingOutput)
{
    famp::workspace::WorkspaceStore store;
    const auto group = store.addEntity(famp::workspace::makeEntity(
        famp::workspace::EntityKind::Group, QStringLiteral("group")));
    QJsonObject json = famp::workspace::serializeSnapshot(store);
    QJsonArray records = json.value(QStringLiteral("entities")).toArray();
    QJsonObject record = records.at(0).toObject();
    record.insert(QStringLiteral("parentId"),
                  group.toString(QUuid::WithoutBraces));
    records[0] = record;
    json.insert(QStringLiteral("entities"), records);

    famp::workspace::WorkspaceSnapshot output;
    output.rootName = QStringLiteral("unchanged");
    QString error;
    EXPECT_FALSE(famp::workspace::deserializeSnapshot(json, output, &error));
    EXPECT_EQ(output.rootName, QStringLiteral("unchanged"));
    EXPECT_FALSE(error.isEmpty());
}

TEST(WorkspaceSnapshotTest, PreservesAnalysisAssetPaths)
{
    famp::workspace::WorkspaceStore source;
    auto dem = famp::workspace::makeEntity(
        famp::workspace::EntityKind::Dem, QStringLiteral("坑底_dem"));
    dem.assetPath = QStringLiteral("/tmp/famp-assets/dem.famp-dem");
    dem.setPayload(std::make_shared<int>(42));
    const auto demId = source.addEntity(dem);
    ASSERT_FALSE(demId.isNull());

    QString error;
    const QJsonObject json = famp::workspace::serializeSnapshot(source, &error);
    ASSERT_FALSE(json.isEmpty()) << error.toStdString();
    famp::workspace::WorkspaceSnapshot snapshot;
    ASSERT_TRUE(famp::workspace::deserializeSnapshot(json, snapshot, &error))
        << error.toStdString();
    ASSERT_EQ(snapshot.entities.size(), 1);
    ASSERT_TRUE(snapshot.entities.front().assetPath.has_value());
    EXPECT_EQ(*snapshot.entities.front().assetPath,
              QStringLiteral("/tmp/famp-assets/dem.famp-dem"));

    famp::workspace::WorkspaceStore restored;
    ASSERT_TRUE(famp::workspace::applySnapshot(snapshot, restored, &error))
        << error.toStdString();
    ASSERT_NE(restored.entity(demId), nullptr);
    ASSERT_TRUE(restored.entity(demId)->assetPath.has_value());
    EXPECT_EQ(*restored.entity(demId)->assetPath,
              QStringLiteral("/tmp/famp-assets/dem.famp-dem"));
}

TEST(WorkspaceSnapshotTest, RoundTripsTwoThousandEntitiesWithStableHierarchy)
{
    constexpr int GroupCount = 25;
    constexpr int ItemsPerGroup = 80;
    famp::workspace::WorkspaceStore source;
    ASSERT_TRUE(source.setName(
        source.rootId(), QStringLiteral("大墓坑高负载项目")));

    QVector<famp::workspace::EntityId> groupIds;
    QVector<QVector<famp::workspace::EntityId>> itemIds;
    groupIds.reserve(GroupCount);
    itemIds.reserve(GroupCount);
    for (int groupIndex = 0; groupIndex < GroupCount; ++groupIndex)
    {
        auto group = famp::workspace::makeEntity(
            famp::workspace::EntityKind::Group,
            QStringLiteral("成果组-%1").arg(groupIndex, 2, 10, QLatin1Char('0')));
        group.display.insert(QStringLiteral("batch"), groupIndex);
        const auto groupId = source.addEntity(group);
        ASSERT_FALSE(groupId.isNull());
        groupIds.append(groupId);

        QVector<famp::workspace::EntityId> groupItems;
        groupItems.reserve(ItemsPerGroup);
        for (int itemIndex = 0; itemIndex < ItemsPerGroup; ++itemIndex)
        {
            auto item = famp::workspace::makeEntity(
                famp::workspace::EntityKind::GraphicsItem,
                QStringLiteral("图元-%1-%2")
                    .arg(groupIndex, 2, 10, QLatin1Char('0'))
                    .arg(itemIndex, 3, 10, QLatin1Char('0')));
            item.visible = (itemIndex % 3) != 0;
            item.locked = (itemIndex % 17) == 0;
            item.display.insert(QStringLiteral("itemIndex"), itemIndex);
            if ((itemIndex % 10) == 0)
            {
                famp::workspace::Provenance provenance;
                provenance.operation = QStringLiteral("stress_simulation");
                provenance.sourceIds = {groupId};
                provenance.parameters.insert(
                    QStringLiteral("sequence"), itemIndex);
                item.provenance = provenance;
            }
            const auto itemId = source.addEntity(item, groupId);
            ASSERT_FALSE(itemId.isNull());
            groupItems.append(itemId);
        }
        itemIds.append(groupItems);
    }
    EXPECT_EQ(source.size(), 1 + GroupCount * (1 + ItemsPerGroup));

    QString error;
    const QJsonObject json = famp::workspace::serializeSnapshot(source, &error);
    ASSERT_FALSE(json.isEmpty()) << error.toStdString();
    famp::workspace::WorkspaceSnapshot snapshot;
    ASSERT_TRUE(famp::workspace::deserializeSnapshot(json, snapshot, &error))
        << error.toStdString();
    EXPECT_EQ(snapshot.entities.size(), GroupCount * (1 + ItemsPerGroup));

    famp::workspace::WorkspaceStore restored;
    ASSERT_TRUE(famp::workspace::applySnapshot(snapshot, restored, &error))
        << error.toStdString();
    EXPECT_EQ(restored.size(), source.size());
    EXPECT_EQ(restored.entity(restored.rootId())->name,
              QStringLiteral("大墓坑高负载项目"));
    for (int groupIndex = 0; groupIndex < GroupCount; ++groupIndex)
    {
        ASSERT_NE(restored.entity(groupIds.at(groupIndex)), nullptr);
        const QVector<famp::workspace::EntityId> children =
            restored.children(groupIds.at(groupIndex));
        EXPECT_EQ(children, itemIds.at(groupIndex));
        ASSERT_EQ(children.size(), ItemsPerGroup);
        const auto* first = restored.entity(children.front());
        ASSERT_NE(first, nullptr);
        EXPECT_FALSE(first->visible);
        EXPECT_TRUE(first->locked);
        ASSERT_TRUE(first->provenance.has_value());
        EXPECT_EQ(first->provenance->operation,
                  QStringLiteral("stress_simulation"));
    }
}

TEST(WorkspaceSnapshotTest, RejectsMoreThanTenThousandEntitiesAtomically)
{
    QJsonObject json = famp::workspace::emptySnapshot();
    QJsonArray records;
    for (int index = 0; index < 10001; ++index)
        records.append(QJsonObject());
    json.insert(QStringLiteral("entities"), records);

    famp::workspace::WorkspaceSnapshot output;
    output.rootName = QStringLiteral("unchanged");
    QString error;
    EXPECT_FALSE(famp::workspace::deserializeSnapshot(json, output, &error));
    EXPECT_EQ(output.rootName, QStringLiteral("unchanged"));
    EXPECT_FALSE(error.isEmpty());
}
