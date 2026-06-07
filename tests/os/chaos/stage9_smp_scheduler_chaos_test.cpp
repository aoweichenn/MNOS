#include <cstddef>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/system/core_topology.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/sched/smp_scheduler.hpp>
#include <support/deterministic_prng.hpp>

namespace cpu_system = mnos::cpu::system;
namespace mm = mnos::os::mm;
namespace sched = mnos::os::sched;
namespace test = mnos::test;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t CHAOS_STAGE9_SMP_SEED = std::uint64_t{0x5A9E'2026ULL};
constexpr std::uint32_t CHAOS_CORE_COUNT = std::uint32_t{4};
constexpr std::size_t CHAOS_THREAD_COUNT = 12;
constexpr std::size_t CHAOS_STEP_COUNT = 256;
constexpr std::uint64_t CHAOS_OPERATION_COUNT = std::uint64_t{6};
constexpr mm::AddressValue CHAOS_STACK_BASE = mm::AddressValue{0x3000000};
constexpr mm::AddressValue CHAOS_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;

[[nodiscard]] cpu_system::CoreId core_from_index(const std::size_t index) noexcept
{
    return cpu_system::CoreId{static_cast<cpu_system::CoreId::value_type>(index % CHAOS_CORE_COUNT)};
}

[[nodiscard]] std::size_t random_thread_index(test::DeterministicPrng& prng)
{
    return static_cast<std::size_t>(prng.next_bounded(CHAOS_THREAD_COUNT));
}

[[nodiscard]] cpu_system::CoreId random_core(test::DeterministicPrng& prng)
{
    return cpu_system::CoreId{static_cast<cpu_system::CoreId::value_type>(prng.next_bounded(CHAOS_CORE_COUNT))};
}
}

TEST(Stage9SmpSchedulerChaosTest, RandomPerCoreMutationsPreserveSchedulerInvariants)
{
    test::DeterministicPrng prng{CHAOS_STAGE9_SMP_SEED};
    sched::SmpScheduler scheduler{cpu_system::CoreTopology{CHAOS_CORE_COUNT}};
    std::vector<sched::ThreadContext> threads;
    threads.reserve(CHAOS_THREAD_COUNT);

    for (std::size_t thread_index = 0; thread_index < CHAOS_THREAD_COUNT; ++thread_index)
    {
        threads.emplace_back(
            sched::ThreadId{sched::THREAD_ID_FIRST_KERNEL_VALUE + static_cast<sched::ThreadId::value_type>(thread_index)},
            mm::VirtualAddress{
                CHAOS_STACK_BASE + static_cast<mm::AddressValue>(thread_index * CHAOS_STACK_STRIDE)});
        scheduler.enqueue(threads.back(), core_from_index(thread_index));
    }

    for (std::uint32_t core_index = std::uint32_t{0}; core_index < CHAOS_CORE_COUNT; ++core_index)
    {
        static_cast<void>(scheduler.schedule_next(cpu_system::CoreId{core_index}));
    }

    for (std::size_t step_index = 0; step_index < CHAOS_STEP_COUNT; ++step_index)
    {
        const std::uint64_t operation = prng.next_bounded(CHAOS_OPERATION_COUNT);
        const cpu_system::CoreId core = random_core(prng);
        if (operation == std::uint64_t{0} && scheduler.has_work_on_core(core))
        {
            static_cast<void>(scheduler.yield_current(core));
        }
        else if (operation == std::uint64_t{1} && scheduler.has_current(core))
        {
            static_cast<void>(scheduler.block_current(core));
        }
        else if (operation == std::uint64_t{2})
        {
            sched::ThreadContext& thread = threads[random_thread_index(prng)];
            if (thread.state() == sched::ThreadState::BLOCKED)
            {
                scheduler.wake(thread, random_core(prng));
            }
        }
        else if (operation == std::uint64_t{3})
        {
            sched::ThreadContext& thread = threads[random_thread_index(prng)];
            if (thread.state() == sched::ThreadState::READY)
            {
                static_cast<void>(scheduler.migrate_ready(thread, random_core(prng)));
            }
        }
        else if (operation == std::uint64_t{4} && scheduler.has_current(core))
        {
            static_cast<void>(scheduler.exit_current(core));
        }
        else
        {
            static_cast<void>(scheduler.rebalance_once());
        }

        std::size_t ready_thread_count = std::size_t{0};
        std::size_t running_thread_count = std::size_t{0};
        for (const sched::ThreadContext& thread : threads)
        {
            if (thread.state() == sched::ThreadState::READY)
            {
                ++ready_thread_count;
                EXPECT_TRUE(scheduler.contains(thread));
            }
            if (thread.state() == sched::ThreadState::RUNNING)
            {
                ++running_thread_count;
                EXPECT_TRUE(scheduler.contains(thread));
            }
            if (thread.state() == sched::ThreadState::BLOCKED || thread.state() == sched::ThreadState::DEAD)
            {
                EXPECT_FALSE(scheduler.contains(thread));
            }
        }

        EXPECT_THAT(scheduler.total_ready_count(), Eq(ready_thread_count));
        EXPECT_THAT(scheduler.total_running_count(), Eq(running_thread_count));
        EXPECT_LE(running_thread_count, static_cast<std::size_t>(CHAOS_CORE_COUNT));
        for (std::uint32_t core_index = std::uint32_t{0}; core_index < CHAOS_CORE_COUNT; ++core_index)
        {
            const cpu_system::CoreId checked_core{core_index};
            if (scheduler.has_current(checked_core))
            {
                const sched::ThreadContext& current_thread = scheduler.current(checked_core);
                EXPECT_THAT(current_thread.state(), Eq(sched::ThreadState::RUNNING));
                EXPECT_THAT(current_thread.cpu_state().core_id(), Eq(checked_core));
            }
        }
    }
}
