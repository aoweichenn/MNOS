#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <mnos/os/sched/thread_context.hpp>

namespace mnos::os::sched
{
using SchedulerTick = std::uint64_t;

class SleepQueue final
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool contains(const ThreadContext& thread) const noexcept;
    [[nodiscard]] std::optional<SchedulerTick> next_wake_tick() const noexcept;

    void sleep_until(ThreadContext& thread, SchedulerTick wake_tick);
    [[nodiscard]] std::vector<ThreadContext*> take_ready(SchedulerTick current_tick);

private:
    struct SleepEntry
    {
        ThreadContext* thread = nullptr;
        SchedulerTick wake_tick = SchedulerTick{0};
    };

    std::deque<SleepEntry> sleepers_;
};
}
