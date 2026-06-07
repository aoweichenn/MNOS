#include <stdexcept>

#include <mnos/os/sched/round_robin_scheduler.hpp>

namespace
{
constexpr const char* ROUND_ROBIN_NO_CURRENT_THREAD_MESSAGE = "round-robin scheduler has no current thread";
constexpr const char* ROUND_ROBIN_DUPLICATE_THREAD_MESSAGE = "round-robin scheduler cannot enqueue a thread twice";
constexpr const char* ROUND_ROBIN_DEAD_THREAD_MESSAGE = "round-robin scheduler cannot enqueue a dead thread";
}

namespace mnos::os::sched
{
bool RoundRobinScheduler::empty() const noexcept
{
    return this->current_ == nullptr && this->ready_queue_.empty();
}

std::size_t RoundRobinScheduler::ready_count() const noexcept
{
    return this->ready_queue_.size();
}

bool RoundRobinScheduler::has_current() const noexcept
{
    return this->current_ != nullptr;
}

ThreadContext& RoundRobinScheduler::current()
{
    if (this->current_ == nullptr)
    {
        throw std::logic_error{ROUND_ROBIN_NO_CURRENT_THREAD_MESSAGE};
    }
    return *this->current_;
}

const ThreadContext& RoundRobinScheduler::current() const
{
    if (this->current_ == nullptr)
    {
        throw std::logic_error{ROUND_ROBIN_NO_CURRENT_THREAD_MESSAGE};
    }
    return *this->current_;
}

void RoundRobinScheduler::enqueue(ThreadContext& thread)
{
    if (!thread.is_alive())
    {
        throw std::logic_error{ROUND_ROBIN_DEAD_THREAD_MESSAGE};
    }
    if (this->current_ == &thread || this->contains(thread))
    {
        throw std::logic_error{ROUND_ROBIN_DUPLICATE_THREAD_MESSAGE};
    }

    this->enqueue_ready(thread);
}

ThreadContext* RoundRobinScheduler::schedule_next()
{
    if (this->current_ != nullptr && this->current_->state() == ThreadState::RUNNING)
    {
        this->current_->set_state(ThreadState::READY);
        this->ready_queue_.push_back(this->current_);
    }
    this->current_ = nullptr;

    while (!this->ready_queue_.empty())
    {
        ThreadContext* const candidate = this->ready_queue_.front();
        this->ready_queue_.pop_front();
        if (candidate->is_runnable())
        {
            candidate->set_state(ThreadState::RUNNING);
            this->current_ = candidate;
            return this->current_;
        }
    }

    return nullptr;
}

ThreadContext* RoundRobinScheduler::yield_current()
{
    return this->schedule_next();
}

ThreadContext* RoundRobinScheduler::block_current()
{
    if (this->current_ == nullptr)
    {
        return nullptr;
    }
    this->current_->set_state(ThreadState::BLOCKED);
    this->current_ = nullptr;
    return this->schedule_next();
}

ThreadContext* RoundRobinScheduler::exit_current()
{
    if (this->current_ == nullptr)
    {
        return nullptr;
    }
    this->current_->set_state(ThreadState::DEAD);
    this->current_ = nullptr;
    return this->schedule_next();
}

void RoundRobinScheduler::wake(ThreadContext& thread)
{
    if (!thread.is_alive())
    {
        throw std::logic_error{ROUND_ROBIN_DEAD_THREAD_MESSAGE};
    }
    if (this->current_ == &thread || this->contains(thread))
    {
        throw std::logic_error{ROUND_ROBIN_DUPLICATE_THREAD_MESSAGE};
    }
    this->enqueue_ready(thread);
}

bool RoundRobinScheduler::contains(const ThreadContext& thread) const noexcept
{
    for (const ThreadContext* const queued_thread : this->ready_queue_)
    {
        if (queued_thread == &thread)
        {
            return true;
        }
    }
    return false;
}

void RoundRobinScheduler::enqueue_ready(ThreadContext& thread)
{
    thread.set_state(ThreadState::READY);
    this->ready_queue_.push_back(&thread);
}
}
