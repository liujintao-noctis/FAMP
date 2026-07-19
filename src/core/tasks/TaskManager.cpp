#include "TaskManager.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace famp::tasks
{
bool Handle::isValid() const
{
    return id != 0 && cancellation;
}

CancellationCheck Handle::cancellationCheck() const
{
    const auto token = cancellation;
    return token
        ? CancellationCheck([token]() {
              return token->load(std::memory_order_relaxed);
          })
        : CancellationCheck{};
}

bool isTerminal(State state)
{
    return state == State::Succeeded
        || state == State::Failed
        || state == State::Cancelled;
}

TaskManager::TaskManager(QObject* parent)
    : QObject(parent)
{
}

Handle TaskManager::start(const QString& name, const QString& message)
{
    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty())
        return {};

    Entry entry;
    entry.cancellation = std::make_shared<std::atomic_bool>(false);
    {
        QMutexLocker locker(&mutex_);
        if (nextId_ == 0)
            return {};
        entry.snapshot.id = nextId_++;
        entry.snapshot.name = normalizedName;
        entry.snapshot.message = message.trimmed();
        entry.snapshot.startedAtUtc = QDateTime::currentDateTimeUtc();
        entries_.insert(entry.snapshot.id, entry);
    }
    emit taskChanged(entry.snapshot.id);
    return Handle{entry.snapshot.id, entry.cancellation};
}

bool TaskManager::setProgress(TaskId id,
                              double progress,
                              const QString& message)
{
    if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0)
        return false;
    {
        QMutexLocker locker(&mutex_);
        auto found = entries_.find(id);
        if (found == entries_.end() || isTerminal(found->snapshot.state))
            return false;
        found->snapshot.progress = progress;
        if (!message.isNull())
            found->snapshot.message = message.trimmed();
    }
    emit taskChanged(id);
    return true;
}

bool TaskManager::requestCancellation(TaskId id)
{
    {
        QMutexLocker locker(&mutex_);
        auto found = entries_.find(id);
        if (found == entries_.end() || isTerminal(found->snapshot.state))
            return false;
        found->cancellation->store(true, std::memory_order_relaxed);
        found->snapshot.cancellationRequested = true;
    }
    emit taskChanged(id);
    return true;
}

bool TaskManager::succeed(TaskId id, const QString& message)
{
    return finish(id, State::Succeeded, message);
}

bool TaskManager::fail(TaskId id, const QString& message)
{
    if (message.trimmed().isEmpty())
        return false;
    return finish(id, State::Failed, message);
}

bool TaskManager::acknowledgeCancellation(TaskId id, const QString& message)
{
    return finish(id, State::Cancelled, message);
}

bool TaskManager::snapshot(TaskId id, Snapshot& result) const
{
    QMutexLocker locker(&mutex_);
    const auto found = entries_.constFind(id);
    if (found == entries_.cend())
        return false;
    result = found->snapshot;
    return true;
}

QVector<Snapshot> TaskManager::snapshots() const
{
    QVector<Snapshot> result;
    {
        QMutexLocker locker(&mutex_);
        result.reserve(entries_.size());
        for (const Entry& entry : entries_)
            result.append(entry.snapshot);
    }
    std::sort(result.begin(), result.end(),
              [](const Snapshot& left, const Snapshot& right) {
                  return left.id < right.id;
              });
    return result;
}

int TaskManager::activeCount() const
{
    QMutexLocker locker(&mutex_);
    int count = 0;
    for (const Entry& entry : entries_)
    {
        if (!isTerminal(entry.snapshot.state))
            ++count;
    }
    return count;
}

int TaskManager::clearFinished()
{
    QMutexLocker locker(&mutex_);
    int removed = 0;
    for (auto iterator = entries_.begin(); iterator != entries_.end();)
    {
        if (isTerminal(iterator->snapshot.state))
        {
            iterator = entries_.erase(iterator);
            ++removed;
        }
        else
        {
            ++iterator;
        }
    }
    return removed;
}

bool TaskManager::finish(TaskId id, State state, const QString& message)
{
    if (!isTerminal(state))
        return false;
    {
        QMutexLocker locker(&mutex_);
        auto found = entries_.find(id);
        if (found == entries_.end() || isTerminal(found->snapshot.state))
            return false;
        if (state == State::Succeeded
            && found->cancellation->load(std::memory_order_relaxed))
        {
            state = State::Cancelled;
        }
        found->snapshot.state = state;
        found->snapshot.progress = state == State::Succeeded
            ? 1.0 : found->snapshot.progress;
        if (!message.isNull())
            found->snapshot.message = message.trimmed();
        found->snapshot.cancellationRequested =
            found->cancellation->load(std::memory_order_relaxed);
        found->snapshot.finishedAtUtc = QDateTime::currentDateTimeUtc();
    }
    emit taskChanged(id);
    emit taskFinished(id);
    return true;
}
}
