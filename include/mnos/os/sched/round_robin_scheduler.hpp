#pragma once

#include <cstddef>
#include <deque>

#include <mnos/os/sched/thread_context.hpp>

namespace mnos::os::sched
{
class RoundRobinScheduler final
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t ready_count() const noexcept;
    [[nodiscard]] bool has_current() const noexcept;
    [[nodiscard]] ThreadContext& current();
    [[nodiscard]] const ThreadContext& current() const;

    void enqueue(ThreadContext& thread);
    [[nodiscard]] ThreadContext* schedule_next();
    [[nodiscard]] ThreadContext* yield_current();
    [[nodiscard]] ThreadContext* block_current();
    [[nodiscard]] ThreadContext* exit_current();
    void wake(ThreadContext& thread);

private:
    [[nodiscard]] bool contains(const ThreadContext& thread) const noexcept;
    void enqueue_ready(ThreadContext& thread);

    std::deque<ThreadContext*> ready_queue_;
    ThreadContext* current_ = nullptr;
};
}
