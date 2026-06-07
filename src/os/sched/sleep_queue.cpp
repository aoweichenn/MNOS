#include <stdexcept>

#include <mnos/os/sched/sleep_queue.hpp>

namespace
{
constexpr const char* SLEEP_QUEUE_DEAD_THREAD_MESSAGE = "sleep queue cannot sleep a dead thread";
constexpr const char* SLEEP_QUEUE_DUPLICATE_THREAD_MESSAGE = "sleep queue cannot sleep a thread twice";
}

namespace mnos::os::sched
{
bool SleepQueue::empty() const noexcept
{
    return this->sleepers_.empty();
}

std::size_t SleepQueue::size() const noexcept
{
    return this->sleepers_.size();
}

bool SleepQueue::contains(const ThreadContext& thread) const noexcept
{
    for (const SleepEntry& sleeper : this->sleepers_)
    {
        if (sleeper.thread == &thread)
        {
            return true;
        }
    }
    return false;
}

std::optional<SchedulerTick> SleepQueue::next_wake_tick() const noexcept
{
    if (this->sleepers_.empty())
    {
        return std::nullopt;
    }
    return this->sleepers_.front().wake_tick;
}

void SleepQueue::sleep_until(ThreadContext& thread, const SchedulerTick wake_tick)
{
    if (!thread.is_alive())
    {
        throw std::logic_error{SLEEP_QUEUE_DEAD_THREAD_MESSAGE};
    }
    if (this->contains(thread))
    {
        throw std::logic_error{SLEEP_QUEUE_DUPLICATE_THREAD_MESSAGE};
    }

    thread.set_state(ThreadState::BLOCKED);
    SleepEntry entry{&thread, wake_tick};
    auto insert_position = this->sleepers_.begin();
    while (insert_position != this->sleepers_.end() && insert_position->wake_tick <= wake_tick)
    {
        ++insert_position;
    }
    this->sleepers_.insert(insert_position, entry);
}

std::vector<ThreadContext*> SleepQueue::take_ready(const SchedulerTick current_tick)
{
    std::vector<ThreadContext*> ready_threads;
    while (!this->sleepers_.empty() && this->sleepers_.front().wake_tick <= current_tick)
    {
        ThreadContext* const thread = this->sleepers_.front().thread;
        this->sleepers_.pop_front();
        if (thread->is_alive())
        {
            ready_threads.push_back(thread);
        }
    }
    return ready_threads;
}
}
