#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#include "TaskManager.h"

TEST(TaskManagerTest, TracksProgressAndSuccessfulCompletion)
{
    famp::tasks::TaskManager manager;
    const auto handle = manager.start(QStringLiteral("加载点云"));
    ASSERT_TRUE(handle.isValid());
    EXPECT_EQ(manager.activeCount(), 1);
    EXPECT_TRUE(manager.setProgress(
        handle.id, 0.5, QStringLiteral("读取属性")));
    EXPECT_TRUE(manager.succeed(handle.id, QStringLiteral("完成")));

    famp::tasks::Snapshot snapshot;
    ASSERT_TRUE(manager.snapshot(handle.id, snapshot));
    EXPECT_EQ(snapshot.state, famp::tasks::State::Succeeded);
    EXPECT_DOUBLE_EQ(snapshot.progress, 1.0);
    EXPECT_EQ(snapshot.message, QStringLiteral("完成"));
    EXPECT_TRUE(snapshot.finishedAtUtc.isValid());
    EXPECT_EQ(manager.activeCount(), 0);
    EXPECT_FALSE(manager.setProgress(handle.id, 0.75));
}

TEST(TaskManagerTest, SharesCancellationWithWorkerAndCannotReportSuccess)
{
    famp::tasks::TaskManager manager;
    const auto handle = manager.start(QStringLiteral("配准"));
    const auto shouldCancel = handle.cancellationCheck();
    ASSERT_TRUE(shouldCancel);
    EXPECT_FALSE(shouldCancel());
    EXPECT_TRUE(manager.requestCancellation(handle.id));
    EXPECT_TRUE(shouldCancel());
    EXPECT_TRUE(manager.succeed(handle.id));

    famp::tasks::Snapshot snapshot;
    ASSERT_TRUE(manager.snapshot(handle.id, snapshot));
    EXPECT_EQ(snapshot.state, famp::tasks::State::Cancelled);
    EXPECT_TRUE(snapshot.cancellationRequested);
}

TEST(TaskManagerTest, RejectsInvalidTransitionsAndClearsOnlyFinishedTasks)
{
    famp::tasks::TaskManager manager;
    EXPECT_FALSE(manager.start(QStringLiteral("   ")).isValid());
    const auto first = manager.start(QStringLiteral("任务一"));
    const auto second = manager.start(QStringLiteral("任务二"));
    EXPECT_FALSE(manager.setProgress(first.id, -0.1));
    EXPECT_FALSE(manager.setProgress(first.id, 1.1));
    EXPECT_FALSE(manager.fail(first.id, QString()));
    EXPECT_TRUE(manager.fail(first.id, QStringLiteral("输入无效")));
    EXPECT_FALSE(manager.acknowledgeCancellation(first.id));
    EXPECT_EQ(manager.clearFinished(), 1);
    EXPECT_EQ(manager.activeCount(), 1);

    const auto snapshots = manager.snapshots();
    ASSERT_EQ(snapshots.size(), 1);
    EXPECT_EQ(snapshots.front().id, second.id);
}

TEST(TaskManagerTest, AllocatesUniqueIdsAcrossWorkerThreads)
{
    famp::tasks::TaskManager manager;
    std::mutex idsMutex;
    std::vector<famp::tasks::TaskId> ids;
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 8; ++worker)
    {
        workers.emplace_back([&]() {
            for (int task = 0; task < 25; ++task)
            {
                const auto handle = manager.start(QStringLiteral("并行任务"));
                std::lock_guard<std::mutex> lock(idsMutex);
                ids.push_back(handle.id);
            }
        });
    }
    for (std::thread& worker : workers)
        worker.join();

    ASSERT_EQ(ids.size(), 200U);
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
    EXPECT_EQ(manager.activeCount(), 200);
}
