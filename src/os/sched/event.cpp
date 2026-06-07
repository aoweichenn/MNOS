#include <mnos/os/sched/event.hpp>

namespace mnos::os::sched
{
Event::Event(const bool signaled) noexcept : signaled_(signaled)
{
}

bool Event::is_signaled() const noexcept
{
    return this->signaled_;
}

std::size_t Event::waiter_count() const noexcept
{
    return this->wait_queue_.size();
}

bool Event::contains(const ThreadContext& thread) const noexcept
{
    return this->wait_queue_.contains(thread);
}

bool Event::wait(ThreadContext& thread)
{
    if (this->signaled_)
    {
        return false;
    }

    this->wait_queue_.wait(thread);
    return true;
}

ThreadContext* Event::signal_one()
{
    this->signaled_ = true;
    return this->wait_queue_.wake_one();
}

std::vector<ThreadContext*> Event::signal_all()
{
    this->signaled_ = true;
    return this->wait_queue_.wake_all();
}

void Event::reset() noexcept
{
    this->signaled_ = false;
}
}
