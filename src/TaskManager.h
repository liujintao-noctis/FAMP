#pragma once

#include "TaskCancellation.h"

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

namespace famp::tasks
{
using TaskId = quint64;

enum class State
{
    Running,
    Succeeded,
    Failed,
    Cancelled
};

struct Snapshot
{
    TaskId id = 0;
    QString name;
    QString message;
    State state = State::Running;
    double progress = 0.0;
    bool cancellationRequested = false;
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
};

struct Handle
{
    TaskId id = 0;
    std::shared_ptr<std::atomic_bool> cancellation;

    bool isValid() const;
    CancellationCheck cancellationCheck() const;
};

bool isTerminal(State state);

class TaskManager : public QObject
{
    Q_OBJECT

public:
    explicit TaskManager(QObject* parent = nullptr);

    Handle start(const QString& name, const QString& message = {});
    bool setProgress(TaskId id,
                     double progress,
                     const QString& message = {});
    bool requestCancellation(TaskId id);
    bool succeed(TaskId id, const QString& message = {});
    bool fail(TaskId id, const QString& message);
    bool acknowledgeCancellation(TaskId id, const QString& message = {});

    bool snapshot(TaskId id, Snapshot& result) const;
    QVector<Snapshot> snapshots() const;
    int activeCount() const;
    int clearFinished();

signals:
    void taskChanged(quint64 id);
    void taskFinished(quint64 id);

private:
    struct Entry
    {
        Snapshot snapshot;
        std::shared_ptr<std::atomic_bool> cancellation;
    };

    bool finish(TaskId id, State state, const QString& message);

    mutable QMutex mutex_;
    QHash<TaskId, Entry> entries_;
    TaskId nextId_ = 1;
};
}
