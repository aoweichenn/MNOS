#include <array>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/page.hpp>
#include <mnos/os/sched/round_robin_scheduler.hpp>
#include <support/deterministic_prng.hpp>

namespace mm = mnos::os::mm;
namespace sched = mnos::os::sched;
namespace test = mnos::test;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t CHAOS_SEED = 0x517A6E55ULL;
constexpr std::size_t CHAOS_THREAD_COUNT = 8;
constexpr std::size_t CHAOS_STEP_COUNT = 128;
constexpr mm::AddressValue CHAOS_STACK_BASE = mm::AddressValue{0x1000000};
constexpr mm::AddressValue CHAOS_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;

[[nodiscard]] std::size_t current_thread_index(
    const std::vector<sched::ThreadContext>& threads,
    const sched::ThreadContext& current_thread) noexcept
{
    for (std::size_t index = 0; index < threads.size(); ++index)
    {
        if (threads[index].id() == current_thread.id())
        {
            return index;
        }
    }
    return threads.size();
}
}

TEST(SchedulerChaosTest, RoundRobinStateMatchesRunnableModel)
{
    test::DeterministicPrng prng{CHAOS_SEED};
    std::vector<sched::ThreadContext> threads;
    threads.reserve(CHAOS_THREAD_COUNT);
    sched::RoundRobinScheduler scheduler;

    for (std::size_t index = 0; index < CHAOS_THREAD_COUNT; ++index)
    {
        threads.emplace_back(
            sched::ThreadId{sched::THREAD_ID_FIRST_KERNEL_VALUE + static_cast<sched::ThreadId::value_type>(index)},
            mm::VirtualAddress{CHAOS_STACK_BASE + static_cast<mm::AddressValue>(index * CHAOS_STACK_STRIDE)});
        scheduler.enqueue(threads.back());
    }

    ASSERT_NE(scheduler.schedule_next(), nullptr);
    for (std::size_t step_index = 0; step_index < CHAOS_STEP_COUNT; ++step_index)
    {
        const std::uint64_t operation = prng.next_bounded(std::uint64_t{4});
        if (operation == std::uint64_t{0} && scheduler.has_current())
        {
            static_cast<void>(scheduler.block_current());
        }
        else if (operation == std::uint64_t{1})
        {
            const std::size_t thread_index = static_cast<std::size_t>(prng.next_bounded(CHAOS_THREAD_COUNT));
            if (threads[thread_index].state() == sched::ThreadState::BLOCKED)
            {
                scheduler.wake(threads[thread_index]);
            }
        }
        else if (operation == std::uint64_t{2} && scheduler.has_current())
        {
            static_cast<void>(scheduler.exit_current());
        }
        else
        {
            static_cast<void>(scheduler.yield_current());
        }

        std::size_t alive_thread_count = std::size_t{0};
        std::size_t running_thread_count = std::size_t{0};
        for (const sched::ThreadContext& thread : threads)
        {
            if (thread.is_alive())
            {
                ++alive_thread_count;
            }
            if (thread.state() == sched::ThreadState::RUNNING)
            {
                ++running_thread_count;
            }
        }
        EXPECT_LE(running_thread_count, std::size_t{1});

        if (scheduler.has_current())
        {
            const std::size_t current_index = current_thread_index(threads, scheduler.current());
            ASSERT_LT(current_index, threads.size());
            EXPECT_THAT(threads[current_index].state(), Eq(sched::ThreadState::RUNNING));
            EXPECT_GT(alive_thread_count, std::size_t{0});
        }
        else
        {
            EXPECT_THAT(running_thread_count, Eq(std::size_t{0}));
        }
    }
}
