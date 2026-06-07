#include <stdexcept>

#include <mnos/os/sched/wait_queue.hpp>

namespace
{
constexpr const char* WAIT_QUEUE_DEAD_THREAD_MESSAGE = "wait queue cannot wait a dead thread";
constexpr const char* WAIT_QUEUE_DUPLICATE_THREAD_MESSAGE = "wait queue cannot wait a thread twice";
}

namespace mnos::os::sched
{
bool WaitQueue::empty() const noexcept
{
    return this->waiters_.empty();
}

std::size_t WaitQueue::size() const noexcept
{
    return this->waiters_.size();
}

bool WaitQueue::contains(const ThreadContext& thread) const noexcept
{
    for (const ThreadContext* const waiter : this->waiters_)
    {
        if (waiter == &thread)
        {
            return true;
        }
    }
    return false;
}

void WaitQueue::wait(ThreadContext& thread)
{
    if (!thread.is_alive())
    {
        throw std::logic_error{WAIT_QUEUE_DEAD_THREAD_MESSAGE};
    }
    if (this->contains(thread))
    {
        throw std::logic_error{WAIT_QUEUE_DUPLICATE_THREAD_MESSAGE};
    }

    thread.set_state(ThreadState::BLOCKED);
    this->waiters_.push_back(&thread);
}

ThreadContext* WaitQueue::wake_one()
{
    while (!this->waiters_.empty())
    {
        ThreadContext* const thread = this->waiters_.front();
        this->waiters_.pop_front();
        if (thread->is_alive())
        {
            return thread;
        }
    }
    return nullptr;
}

std::vector<ThreadContext*> WaitQueue::wake_all()
{
    std::vector<ThreadContext*> ready_threads;
    while (ThreadContext* const thread = this->wake_one())
    {
        ready_threads.push_back(thread);
    }
    return ready_threads;
}
}
