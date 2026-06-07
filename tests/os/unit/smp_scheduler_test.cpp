#include <cstddef>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/system/core_topology.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/sched/smp_scheduler.hpp>

namespace cpu_system = mnos::cpu::system;
namespace mm = mnos::os::mm;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::uint32_t TEST_CORE_COUNT = std::uint32_t{3};
constexpr cpu_system::CoreId TEST_CORE_0{0};
constexpr cpu_system::CoreId TEST_CORE_1{1};
constexpr cpu_system::CoreId TEST_CORE_2{2};
constexpr cpu_system::CoreId TEST_INVALID_CORE{99};
constexpr sched::ThreadId::value_type TEST_THREAD_ID_BASE = sched::THREAD_ID_FIRST_KERNEL_VALUE;
constexpr std::size_t TEST_THREAD_COUNT = 6;
constexpr mm::AddressValue TEST_STACK_BASE = mm::AddressValue{0x200000};
constexpr mm::AddressValue TEST_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;

[[nodiscard]] sched::ThreadContext make_thread(const std::size_t index)
{
    return sched::ThreadContext{
        sched::ThreadId{TEST_THREAD_ID_BASE + static_cast<sched::ThreadId::value_type>(index)},
        mm::VirtualAddress{TEST_STACK_BASE + static_cast<mm::AddressValue>(index * TEST_STACK_STRIDE)}};
}

[[nodiscard]] cpu_system::CoreTopology make_topology()
{
    return cpu_system::CoreTopology{TEST_CORE_COUNT};
}
}

TEST(SmpSchedulerTest, SchedulesIndependentPerCoreRunQueues)
{
    std::vector<sched::ThreadContext> threads;
    threads.reserve(TEST_THREAD_COUNT);
    for (std::size_t index = 0; index < TEST_THREAD_COUNT; ++index)
    {
        threads.push_back(make_thread(index));
    }
    sched::SmpScheduler scheduler{make_topology()};

    EXPECT_TRUE(scheduler.empty());
    scheduler.enqueue(threads[0], TEST_CORE_0);
    scheduler.enqueue(threads[1], TEST_CORE_0);
    scheduler.enqueue(threads[2], TEST_CORE_1);
    EXPECT_FALSE(scheduler.empty());
    EXPECT_THAT(scheduler.core_ready_count(TEST_CORE_0), Eq(std::size_t{2}));
    EXPECT_THAT(scheduler.core_ready_count(TEST_CORE_1), Eq(std::size_t{1}));
    EXPECT_THROW(scheduler.enqueue(threads[0], TEST_CORE_2), std::logic_error);

    ASSERT_NE(scheduler.schedule_next(TEST_CORE_0), nullptr);
    EXPECT_THAT(scheduler.current(TEST_CORE_0).id(), Eq(threads[0].id()));
    EXPECT_THAT(threads[0].cpu_state().core_id(), Eq(TEST_CORE_0));
    EXPECT_THAT(threads[0].state(), Eq(sched::ThreadState::RUNNING));

    ASSERT_NE(scheduler.schedule_next(TEST_CORE_1), nullptr);
    EXPECT_THAT(scheduler.current(TEST_CORE_1).id(), Eq(threads[2].id()));
    EXPECT_THAT(scheduler.total_running_count(), Eq(std::size_t{2}));

    ASSERT_NE(scheduler.yield_current(TEST_CORE_0), nullptr);
    EXPECT_THAT(scheduler.current(TEST_CORE_0).id(), Eq(threads[1].id()));
    EXPECT_THAT(threads[0].state(), Eq(sched::ThreadState::READY));

    EXPECT_EQ(scheduler.block_current(TEST_CORE_1), nullptr);
    EXPECT_FALSE(scheduler.has_current(TEST_CORE_1));
    EXPECT_THAT(threads[2].state(), Eq(sched::ThreadState::BLOCKED));

    scheduler.wake(threads[2], TEST_CORE_2);
    ASSERT_NE(scheduler.schedule_next(TEST_CORE_2), nullptr);
    EXPECT_THAT(scheduler.current(TEST_CORE_2).id(), Eq(threads[2].id()));
    EXPECT_THAT(threads[2].cpu_state().core_id(), Eq(TEST_CORE_2));
}

TEST(SmpSchedulerTest, MigratesStealsAndRebalancesReadyThreads)
{
    std::vector<sched::ThreadContext> threads;
    threads.reserve(TEST_THREAD_COUNT);
    for (std::size_t index = 0; index < TEST_THREAD_COUNT; ++index)
    {
        threads.push_back(make_thread(index));
    }
    sched::SmpScheduler scheduler{make_topology()};

    scheduler.enqueue(threads[0], TEST_CORE_0);
    scheduler.enqueue(threads[1], TEST_CORE_0);
    scheduler.enqueue(threads[2], TEST_CORE_0);
    scheduler.enqueue(threads[3], TEST_CORE_0);
    ASSERT_NE(scheduler.schedule_next(TEST_CORE_0), nullptr);

    const sched::ThreadMigration migration = scheduler.migrate_ready(threads[1], TEST_CORE_1);
    EXPECT_THAT(migration.source_core(), Eq(TEST_CORE_0));
    EXPECT_THAT(migration.target_core(), Eq(TEST_CORE_1));
    EXPECT_THAT(migration.thread_id(), Eq(threads[1].id()));
    EXPECT_THAT(threads[1].cpu_state().core_id(), Eq(TEST_CORE_1));
    EXPECT_THROW(static_cast<void>(scheduler.migrate_ready(threads[0], TEST_CORE_2)), std::logic_error);

    const std::optional<sched::ThreadMigration> stolen = scheduler.steal_one(TEST_CORE_0, TEST_CORE_2);
    ASSERT_TRUE(stolen.has_value());
    EXPECT_THAT(stolen->source_core(), Eq(TEST_CORE_0));
    EXPECT_THAT(stolen->target_core(), Eq(TEST_CORE_2));
    EXPECT_THAT(scheduler.core_ready_count(TEST_CORE_2), Eq(std::size_t{1}));

    scheduler.enqueue(threads[4], TEST_CORE_0);
    scheduler.enqueue(threads[5], TEST_CORE_0);
    const std::optional<sched::ThreadMigration> rebalanced = scheduler.rebalance_once();
    ASSERT_TRUE(rebalanced.has_value());
    EXPECT_THAT(rebalanced->source_core(), Eq(TEST_CORE_0));
    EXPECT_FALSE(rebalanced->target_core() == TEST_CORE_0);
}

TEST(SmpSchedulerTest, AutoPlacementAndQueryContractsTrackReadyAndRunningThreads)
{
    sched::ThreadContext first_thread = make_thread(std::size_t{0});
    sched::ThreadContext second_thread = make_thread(std::size_t{1});
    sched::ThreadContext third_thread = make_thread(std::size_t{2});
    sched::ThreadContext fourth_thread = make_thread(std::size_t{3});
    sched::SmpScheduler scheduler{make_topology()};
    const sched::SmpScheduler& const_scheduler = scheduler;

    EXPECT_THAT(scheduler.core_count(), Eq(static_cast<std::size_t>(TEST_CORE_COUNT)));
    EXPECT_THAT(scheduler.topology().core_count(), Eq(TEST_CORE_COUNT));
    EXPECT_FALSE(scheduler.has_work_on_core(TEST_CORE_0));
    EXPECT_EQ(scheduler.block_current(TEST_CORE_0), nullptr);
    EXPECT_EQ(scheduler.exit_current(TEST_CORE_1), nullptr);
    EXPECT_FALSE(scheduler.steal_one(TEST_CORE_0, TEST_CORE_1).has_value());
    EXPECT_FALSE(scheduler.current_core_of(first_thread).has_value());

    const cpu_system::CoreId first_core = scheduler.enqueue(first_thread);
    const cpu_system::CoreId second_core = scheduler.enqueue(second_thread);
    const cpu_system::CoreId third_core = scheduler.enqueue(third_thread);

    EXPECT_THAT(first_core, Eq(TEST_CORE_0));
    EXPECT_THAT(second_core, Eq(TEST_CORE_1));
    EXPECT_THAT(third_core, Eq(TEST_CORE_2));
    EXPECT_TRUE(scheduler.contains(first_thread));
    EXPECT_THAT(scheduler.total_ready_count(), Eq(std::size_t{3}));
    ASSERT_TRUE(scheduler.current_core_of(second_thread).has_value());
    EXPECT_THAT(*scheduler.current_core_of(second_thread), Eq(TEST_CORE_1));

    ASSERT_NE(scheduler.schedule_next(TEST_CORE_1), nullptr);
    EXPECT_THAT(const_scheduler.current(TEST_CORE_1).id(), Eq(second_thread.id()));
    EXPECT_THROW(static_cast<void>(const_scheduler.current(TEST_CORE_0)), std::logic_error);
    ASSERT_TRUE(scheduler.current_core_of(second_thread).has_value());
    EXPECT_THAT(*scheduler.current_core_of(second_thread), Eq(TEST_CORE_1));

    fourth_thread.set_state(sched::ThreadState::BLOCKED);
    const cpu_system::CoreId wake_core = scheduler.wake(fourth_thread);
    EXPECT_TRUE(scheduler.contains(fourth_thread));
    EXPECT_TRUE(scheduler.current_core_of(fourth_thread).has_value());
    EXPECT_THAT(*scheduler.current_core_of(fourth_thread), Eq(wake_core));
}

TEST(SmpSchedulerTest, RejectsInvalidStatesAndRecordsPerCoreTimerCounters)
{
    sched::ThreadContext first_thread = make_thread(std::size_t{0});
    sched::ThreadContext second_thread = make_thread(std::size_t{1});
    sched::ThreadContext third_thread = make_thread(std::size_t{2});
    sched::SmpScheduler scheduler{make_topology()};

    EXPECT_THROW(static_cast<void>(scheduler.current(TEST_CORE_0)), std::logic_error);
    EXPECT_THROW(static_cast<void>(scheduler.core_ready_count(TEST_INVALID_CORE)), std::out_of_range);

    third_thread.set_state(sched::ThreadState::DEAD);
    EXPECT_THROW(scheduler.enqueue(third_thread, TEST_CORE_0), std::logic_error);

    scheduler.enqueue(first_thread, TEST_CORE_0);
    ASSERT_NE(scheduler.schedule_next(TEST_CORE_0), nullptr);
    EXPECT_THROW(scheduler.enqueue(first_thread, TEST_CORE_1), std::logic_error);

    second_thread.set_state(sched::ThreadState::RUNNING);
    EXPECT_THROW(scheduler.enqueue(second_thread, TEST_CORE_1), std::logic_error);

    scheduler.record_timer_tick(TEST_CORE_0, true);
    scheduler.record_timer_tick(TEST_CORE_1, false);
    EXPECT_THAT(scheduler.core_timer_tick_count(TEST_CORE_0), Eq(sched::SchedulerTick{1}));
    EXPECT_THAT(scheduler.core_preemption_count(TEST_CORE_0), Eq(std::size_t{1}));
    EXPECT_THAT(scheduler.core_timer_tick_count(TEST_CORE_1), Eq(sched::SchedulerTick{1}));
    EXPECT_THAT(scheduler.core_preemption_count(TEST_CORE_1), Eq(std::size_t{0}));
}
