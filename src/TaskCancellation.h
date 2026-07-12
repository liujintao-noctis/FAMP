#pragma once

#include <functional>

namespace famp::tasks
{
using CancellationCheck = std::function<bool()>;

inline bool isCancellationRequested(const CancellationCheck& check)
{
    return check && check();
}
}
