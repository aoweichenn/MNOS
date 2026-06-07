#pragma once

#include <cstddef>
#include <vector>

#include <mnos/os/sched/wait_queue.hpp>

namespace mnos::os::sched
{
class Event final
{
public:
    explicit Event(bool signaled = false) noexcept;

    [[nodiscard]] bool is_signaled() const noexcept;
    [[nodiscard]] std::size_t waiter_count() const noexcept;
    [[nodiscard]] bool contains(const ThreadContext& thread) const noexcept;

    [[nodiscard]] bool wait(ThreadContext& thread);
    [[nodiscard]] ThreadContext* signal_one();
    [[nodiscard]] std::vector<ThreadContext*> signal_all();
    void reset() noexcept;

private:
    bool signaled_;
    WaitQueue wait_queue_;
};
}
