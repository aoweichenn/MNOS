#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

#include <mnos/cpu/system/core_id.hpp>
#include <mnos/cpu/system/core_topology.hpp>
#include <mnos/os/sched/sleep_queue.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace mnos::os::sched
{
class ThreadMigration final
{
public:
    ThreadMigration(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        ThreadId thread_id) noexcept;

    [[nodiscard]] cpu::system::CoreId source_core() const noexcept;
    [[nodiscard]] cpu::system::CoreId target_core() const noexcept;
    [[nodiscard]] ThreadId thread_id() const noexcept;

private:
    cpu::system::CoreId source_core_;
    cpu::system::CoreId target_core_;
    ThreadId thread_id_;
};

class SmpScheduler final
{
public:
    explicit SmpScheduler(cpu::system::CoreTopology topology);

    [[nodiscard]] const cpu::system::CoreTopology& topology() const noexcept;
    [[nodiscard]] std::size_t core_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool contains(const ThreadContext& thread) const noexcept;
    [[nodiscard]] bool has_work_on_core(cpu::system::CoreId core_id) const;
    [[nodiscard]] std::size_t total_ready_count() const noexcept;
    [[nodiscard]] std::size_t total_running_count() const noexcept;
    [[nodiscard]] std::size_t core_ready_count(cpu::system::CoreId core_id) const;
    [[nodiscard]] std::size_t core_load(cpu::system::CoreId core_id) const;
    [[nodiscard]] SchedulerTick core_timer_tick_count(cpu::system::CoreId core_id) const;
    [[nodiscard]] std::size_t core_preemption_count(cpu::system::CoreId core_id) const;
    [[nodiscard]] bool has_current(cpu::system::CoreId core_id) const;
    [[nodiscard]] ThreadContext& current(cpu::system::CoreId core_id);
    [[nodiscard]] const ThreadContext& current(cpu::system::CoreId core_id) const;
    [[nodiscard]] std::optional<cpu::system::CoreId> current_core_of(const ThreadContext& thread) const noexcept;

    [[nodiscard]] cpu::system::CoreId enqueue(ThreadContext& thread);
    void enqueue(ThreadContext& thread, cpu::system::CoreId target_core);
    [[nodiscard]] ThreadContext* schedule_next(cpu::system::CoreId core_id);
    [[nodiscard]] ThreadContext* yield_current(cpu::system::CoreId core_id);
    [[nodiscard]] ThreadContext* block_current(cpu::system::CoreId core_id);
    [[nodiscard]] ThreadContext* exit_current(cpu::system::CoreId core_id);
    [[nodiscard]] cpu::system::CoreId wake(ThreadContext& thread);
    void wake(ThreadContext& thread, cpu::system::CoreId target_core);
    [[nodiscard]] ThreadMigration migrate_ready(ThreadContext& thread, cpu::system::CoreId target_core);
    [[nodiscard]] std::optional<ThreadMigration> steal_one(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core);
    [[nodiscard]] std::optional<ThreadMigration> rebalance_once();
    void record_timer_tick(cpu::system::CoreId core_id, bool preempted);

private:
    class CoreRunQueue final
    {
    public:
        explicit CoreRunQueue(cpu::system::CoreId core_id) noexcept;

        [[nodiscard]] cpu::system::CoreId core_id() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] bool contains(const ThreadContext& thread) const noexcept;
        [[nodiscard]] bool contains_ready(const ThreadContext& thread) const noexcept;
        [[nodiscard]] bool has_work() const noexcept;
        [[nodiscard]] std::size_t ready_count() const noexcept;
        [[nodiscard]] std::size_t load() const noexcept;
        [[nodiscard]] SchedulerTick timer_tick_count() const noexcept;
        [[nodiscard]] std::size_t preemption_count() const noexcept;
        [[nodiscard]] bool has_current() const noexcept;
        [[nodiscard]] ThreadContext& current();
        [[nodiscard]] const ThreadContext& current() const;

        void enqueue_ready(ThreadContext& thread);
        [[nodiscard]] ThreadContext* schedule_next();
        [[nodiscard]] ThreadContext* block_current();
        [[nodiscard]] ThreadContext* exit_current();
        void remove_ready(ThreadContext& thread) noexcept;
        [[nodiscard]] ThreadContext* take_migratable_back() noexcept;
        void record_timer_tick(bool preempted) noexcept;

    private:
        [[nodiscard]] ThreadContext* take_next_runnable() noexcept;
        void bind_thread_to_core(ThreadContext& thread) const noexcept;

        cpu::system::CoreId core_id_;
        std::deque<ThreadContext*> ready_queue_;
        ThreadContext* current_ = nullptr;
        SchedulerTick timer_tick_count_ = SchedulerTick{0};
        std::size_t preemption_count_ = 0;
    };

    [[nodiscard]] CoreRunQueue& queue_for_core(cpu::system::CoreId core_id);
    [[nodiscard]] const CoreRunQueue& queue_for_core(cpu::system::CoreId core_id) const;
    [[nodiscard]] std::size_t queue_index(cpu::system::CoreId core_id) const;
    [[nodiscard]] CoreRunQueue& least_loaded_queue();
    [[nodiscard]] CoreRunQueue& busiest_ready_queue();
    [[nodiscard]] CoreRunQueue* queue_containing_ready(ThreadContext& thread) noexcept;
    void ensure_can_enqueue(const ThreadContext& thread) const;

    cpu::system::CoreTopology topology_;
    std::vector<CoreRunQueue> core_queues_;
};
}
