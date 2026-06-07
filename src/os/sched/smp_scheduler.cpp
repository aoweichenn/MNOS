#include <stdexcept>

#include <mnos/os/sched/smp_scheduler.hpp>

namespace
{
constexpr const char* SMP_SCHEDULER_CORE_OUT_OF_RANGE_MESSAGE = "smp scheduler core id is outside the topology";
constexpr const char* SMP_SCHEDULER_NO_CURRENT_THREAD_MESSAGE = "smp scheduler core has no current thread";
constexpr const char* SMP_SCHEDULER_DUPLICATE_THREAD_MESSAGE = "smp scheduler cannot enqueue a thread twice";
constexpr const char* SMP_SCHEDULER_DEAD_THREAD_MESSAGE = "smp scheduler cannot enqueue a dead thread";
constexpr const char* SMP_SCHEDULER_RUNNING_THREAD_MESSAGE = "smp scheduler cannot enqueue a running thread";
constexpr const char* SMP_SCHEDULER_MIGRATION_NOT_READY_MESSAGE =
    "smp scheduler can only migrate a queued ready thread";
}

namespace mnos::os::sched
{
ThreadMigration::ThreadMigration(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    const ThreadId thread_id) noexcept :
    source_core_(source_core),
    target_core_(target_core),
    thread_id_(thread_id)
{
}

cpu::system::CoreId ThreadMigration::source_core() const noexcept
{
    return this->source_core_;
}

cpu::system::CoreId ThreadMigration::target_core() const noexcept
{
    return this->target_core_;
}

ThreadId ThreadMigration::thread_id() const noexcept
{
    return this->thread_id_;
}

SmpScheduler::CoreRunQueue::CoreRunQueue(const cpu::system::CoreId core_id) noexcept : core_id_(core_id)
{
}

cpu::system::CoreId SmpScheduler::CoreRunQueue::core_id() const noexcept
{
    return this->core_id_;
}

bool SmpScheduler::CoreRunQueue::empty() const noexcept
{
    return this->current_ == nullptr && this->ready_queue_.empty();
}

bool SmpScheduler::CoreRunQueue::contains(const ThreadContext& thread) const noexcept
{
    return this->current_ == &thread || this->contains_ready(thread);
}

bool SmpScheduler::CoreRunQueue::contains_ready(const ThreadContext& thread) const noexcept
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

bool SmpScheduler::CoreRunQueue::has_work() const noexcept
{
    return this->current_ != nullptr || !this->ready_queue_.empty();
}

std::size_t SmpScheduler::CoreRunQueue::ready_count() const noexcept
{
    return this->ready_queue_.size();
}

std::size_t SmpScheduler::CoreRunQueue::load() const noexcept
{
    return this->ready_queue_.size() + (this->current_ != nullptr ? std::size_t{1} : std::size_t{0});
}

SchedulerTick SmpScheduler::CoreRunQueue::timer_tick_count() const noexcept
{
    return this->timer_tick_count_;
}

std::size_t SmpScheduler::CoreRunQueue::preemption_count() const noexcept
{
    return this->preemption_count_;
}

bool SmpScheduler::CoreRunQueue::has_current() const noexcept
{
    return this->current_ != nullptr;
}

ThreadContext& SmpScheduler::CoreRunQueue::current()
{
    if (this->current_ == nullptr)
    {
        throw std::logic_error{SMP_SCHEDULER_NO_CURRENT_THREAD_MESSAGE};
    }
    return *this->current_;
}

const ThreadContext& SmpScheduler::CoreRunQueue::current() const
{
    if (this->current_ == nullptr)
    {
        throw std::logic_error{SMP_SCHEDULER_NO_CURRENT_THREAD_MESSAGE};
    }
    return *this->current_;
}

void SmpScheduler::CoreRunQueue::enqueue_ready(ThreadContext& thread)
{
    this->bind_thread_to_core(thread);
    thread.set_state(ThreadState::READY);
    this->ready_queue_.push_back(&thread);
}

ThreadContext* SmpScheduler::CoreRunQueue::schedule_next()
{
    if (this->current_ != nullptr && this->current_->state() == ThreadState::RUNNING)
    {
        this->current_->set_state(ThreadState::READY);
        this->ready_queue_.push_back(this->current_);
    }
    this->current_ = nullptr;

    ThreadContext* const next_thread = this->take_next_runnable();
    if (next_thread == nullptr)
    {
        return nullptr;
    }

    this->bind_thread_to_core(*next_thread);
    next_thread->set_state(ThreadState::RUNNING);
    this->current_ = next_thread;
    return this->current_;
}

ThreadContext* SmpScheduler::CoreRunQueue::block_current()
{
    if (this->current_ == nullptr)
    {
        return nullptr;
    }

    this->current_->set_state(ThreadState::BLOCKED);
    this->current_ = nullptr;
    return this->schedule_next();
}

ThreadContext* SmpScheduler::CoreRunQueue::exit_current()
{
    if (this->current_ == nullptr)
    {
        return nullptr;
    }

    this->current_->set_state(ThreadState::DEAD);
    this->current_ = nullptr;
    return this->schedule_next();
}

void SmpScheduler::CoreRunQueue::remove_ready(ThreadContext& thread) noexcept
{
    for (auto thread_iterator = this->ready_queue_.begin();
         thread_iterator != this->ready_queue_.end();
         ++thread_iterator)
    {
        if (*thread_iterator == &thread)
        {
            this->ready_queue_.erase(thread_iterator);
            return;
        }
    }
}

ThreadContext* SmpScheduler::CoreRunQueue::take_migratable_back() noexcept
{
    while (!this->ready_queue_.empty())
    {
        ThreadContext* const thread = this->ready_queue_.back();
        this->ready_queue_.pop_back();
        if (thread->is_runnable())
        {
            return thread;
        }
    }
    return nullptr;
}

void SmpScheduler::CoreRunQueue::record_timer_tick(const bool preempted) noexcept
{
    ++this->timer_tick_count_;
    if (preempted)
    {
        ++this->preemption_count_;
    }
}

ThreadContext* SmpScheduler::CoreRunQueue::take_next_runnable() noexcept
{
    while (!this->ready_queue_.empty())
    {
        ThreadContext* const candidate = this->ready_queue_.front();
        this->ready_queue_.pop_front();
        if (candidate->is_runnable())
        {
            return candidate;
        }
    }
    return nullptr;
}

void SmpScheduler::CoreRunQueue::bind_thread_to_core(ThreadContext& thread) const noexcept
{
    thread.cpu_state().set_core_id(this->core_id_);
}

SmpScheduler::SmpScheduler(cpu::system::CoreTopology topology) : topology_(topology)
{
    this->core_queues_.reserve(this->topology_.core_count());
    for (std::uint32_t core_index = std::uint32_t{0}; core_index < this->topology_.core_count(); ++core_index)
    {
        this->core_queues_.emplace_back(this->topology_.core_at(core_index));
    }
}

const cpu::system::CoreTopology& SmpScheduler::topology() const noexcept
{
    return this->topology_;
}

std::size_t SmpScheduler::core_count() const noexcept
{
    return this->core_queues_.size();
}

bool SmpScheduler::empty() const noexcept
{
    for (const CoreRunQueue& queue : this->core_queues_)
    {
        if (!queue.empty())
        {
            return false;
        }
    }
    return true;
}

bool SmpScheduler::contains(const ThreadContext& thread) const noexcept
{
    for (const CoreRunQueue& queue : this->core_queues_)
    {
        if (queue.contains(thread))
        {
            return true;
        }
    }
    return false;
}

bool SmpScheduler::has_work_on_core(const cpu::system::CoreId core_id) const
{
    return this->queue_for_core(core_id).has_work();
}

std::size_t SmpScheduler::total_ready_count() const noexcept
{
    std::size_t ready_count = std::size_t{0};
    for (const CoreRunQueue& queue : this->core_queues_)
    {
        ready_count += queue.ready_count();
    }
    return ready_count;
}

std::size_t SmpScheduler::total_running_count() const noexcept
{
    std::size_t running_count = std::size_t{0};
    for (const CoreRunQueue& queue : this->core_queues_)
    {
        if (queue.has_current())
        {
            ++running_count;
        }
    }
    return running_count;
}

std::size_t SmpScheduler::core_ready_count(const cpu::system::CoreId core_id) const
{
    return this->queue_for_core(core_id).ready_count();
}

std::size_t SmpScheduler::core_load(const cpu::system::CoreId core_id) const
{
    return this->queue_for_core(core_id).load();
}

SchedulerTick SmpScheduler::core_timer_tick_count(const cpu::system::CoreId core_id) const
{
    return this->queue_for_core(core_id).timer_tick_count();
}

std::size_t SmpScheduler::core_preemption_count(const cpu::system::CoreId core_id) const
{
    return this->queue_for_core(core_id).preemption_count();
}

bool SmpScheduler::has_current(const cpu::system::CoreId core_id) const
{
    return this->queue_for_core(core_id).has_current();
}

ThreadContext& SmpScheduler::current(const cpu::system::CoreId core_id)
{
    return this->queue_for_core(core_id).current();
}

const ThreadContext& SmpScheduler::current(const cpu::system::CoreId core_id) const
{
    return this->queue_for_core(core_id).current();
}

std::optional<cpu::system::CoreId> SmpScheduler::current_core_of(const ThreadContext& thread) const noexcept
{
    for (const CoreRunQueue& queue : this->core_queues_)
    {
        if (queue.contains(thread))
        {
            return queue.core_id();
        }
    }
    return std::nullopt;
}

cpu::system::CoreId SmpScheduler::enqueue(ThreadContext& thread)
{
    this->ensure_can_enqueue(thread);
    CoreRunQueue& queue = this->least_loaded_queue();
    queue.enqueue_ready(thread);
    return queue.core_id();
}

void SmpScheduler::enqueue(ThreadContext& thread, const cpu::system::CoreId target_core)
{
    this->ensure_can_enqueue(thread);
    this->queue_for_core(target_core).enqueue_ready(thread);
}

ThreadContext* SmpScheduler::schedule_next(const cpu::system::CoreId core_id)
{
    return this->queue_for_core(core_id).schedule_next();
}

ThreadContext* SmpScheduler::yield_current(const cpu::system::CoreId core_id)
{
    return this->queue_for_core(core_id).schedule_next();
}

ThreadContext* SmpScheduler::block_current(const cpu::system::CoreId core_id)
{
    return this->queue_for_core(core_id).block_current();
}

ThreadContext* SmpScheduler::exit_current(const cpu::system::CoreId core_id)
{
    return this->queue_for_core(core_id).exit_current();
}

cpu::system::CoreId SmpScheduler::wake(ThreadContext& thread)
{
    this->ensure_can_enqueue(thread);
    CoreRunQueue& queue = this->least_loaded_queue();
    queue.enqueue_ready(thread);
    return queue.core_id();
}

void SmpScheduler::wake(ThreadContext& thread, const cpu::system::CoreId target_core)
{
    this->ensure_can_enqueue(thread);
    this->queue_for_core(target_core).enqueue_ready(thread);
}

ThreadMigration SmpScheduler::migrate_ready(
    ThreadContext& thread,
    const cpu::system::CoreId target_core)
{
    CoreRunQueue* const source_queue = this->queue_containing_ready(thread);
    if (source_queue == nullptr || thread.state() != ThreadState::READY)
    {
        throw std::logic_error{SMP_SCHEDULER_MIGRATION_NOT_READY_MESSAGE};
    }
    CoreRunQueue& target_queue = this->queue_for_core(target_core);
    const ThreadMigration migration{source_queue->core_id(), target_queue.core_id(), thread.id()};
    if (source_queue == &target_queue)
    {
        return migration;
    }

    source_queue->remove_ready(thread);
    target_queue.enqueue_ready(thread);
    return migration;
}

std::optional<ThreadMigration> SmpScheduler::steal_one(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core)
{
    CoreRunQueue& source_queue = this->queue_for_core(source_core);
    CoreRunQueue& target_queue = this->queue_for_core(target_core);
    ThreadContext* const thread = source_queue.take_migratable_back();
    if (thread == nullptr)
    {
        return std::nullopt;
    }

    target_queue.enqueue_ready(*thread);
    return ThreadMigration{source_queue.core_id(), target_queue.core_id(), thread->id()};
}

std::optional<ThreadMigration> SmpScheduler::rebalance_once()
{
    CoreRunQueue& source_queue = this->busiest_ready_queue();
    CoreRunQueue& target_queue = this->least_loaded_queue();
    if (source_queue.core_id() == target_queue.core_id() ||
        source_queue.ready_count() <= target_queue.load() + std::size_t{1})
    {
        return std::nullopt;
    }

    return this->steal_one(source_queue.core_id(), target_queue.core_id());
}

void SmpScheduler::record_timer_tick(const cpu::system::CoreId core_id, const bool preempted)
{
    this->queue_for_core(core_id).record_timer_tick(preempted);
}

SmpScheduler::CoreRunQueue& SmpScheduler::queue_for_core(const cpu::system::CoreId core_id)
{
    return this->core_queues_[this->queue_index(core_id)];
}

const SmpScheduler::CoreRunQueue& SmpScheduler::queue_for_core(const cpu::system::CoreId core_id) const
{
    return this->core_queues_[this->queue_index(core_id)];
}

std::size_t SmpScheduler::queue_index(const cpu::system::CoreId core_id) const
{
    if (!this->topology_.contains(core_id))
    {
        throw std::out_of_range{SMP_SCHEDULER_CORE_OUT_OF_RANGE_MESSAGE};
    }
    return static_cast<std::size_t>(core_id.value());
}

SmpScheduler::CoreRunQueue& SmpScheduler::least_loaded_queue()
{
    std::size_t queue_index = std::size_t{0};
    for (std::size_t index = std::size_t{1}; index < this->core_queues_.size(); ++index)
    {
        if (this->core_queues_[index].load() < this->core_queues_[queue_index].load())
        {
            queue_index = index;
        }
    }
    return this->core_queues_[queue_index];
}

SmpScheduler::CoreRunQueue& SmpScheduler::busiest_ready_queue()
{
    std::size_t queue_index = std::size_t{0};
    for (std::size_t index = std::size_t{1}; index < this->core_queues_.size(); ++index)
    {
        if (this->core_queues_[index].ready_count() > this->core_queues_[queue_index].ready_count())
        {
            queue_index = index;
        }
    }
    return this->core_queues_[queue_index];
}

SmpScheduler::CoreRunQueue* SmpScheduler::queue_containing_ready(ThreadContext& thread) noexcept
{
    for (CoreRunQueue& queue : this->core_queues_)
    {
        if (queue.contains_ready(thread))
        {
            return &queue;
        }
    }
    return nullptr;
}

void SmpScheduler::ensure_can_enqueue(const ThreadContext& thread) const
{
    if (!thread.is_alive())
    {
        throw std::logic_error{SMP_SCHEDULER_DEAD_THREAD_MESSAGE};
    }
    if (thread.state() == ThreadState::RUNNING)
    {
        throw std::logic_error{SMP_SCHEDULER_RUNNING_THREAD_MESSAGE};
    }
    if (this->contains(thread))
    {
        throw std::logic_error{SMP_SCHEDULER_DUPLICATE_THREAD_MESSAGE};
    }
}
}
