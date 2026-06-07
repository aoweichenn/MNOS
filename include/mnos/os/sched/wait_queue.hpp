#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include <mnos/os/sched/thread_context.hpp>

namespace mnos::os::sched
{
class WaitQueue final
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool contains(const ThreadContext& thread) const noexcept;

    void wait(ThreadContext& thread);
    [[nodiscard]] ThreadContext* wake_one();
    [[nodiscard]] std::vector<ThreadContext*> wake_all();

private:
    std::deque<ThreadContext*> waiters_;
};
}
