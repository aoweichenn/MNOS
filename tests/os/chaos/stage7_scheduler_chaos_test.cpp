#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/page.hpp>
#include <mnos/os/sched/sleep_queue.hpp>
#include <support/deterministic_prng.hpp>

namespace mm = mnos::os::mm;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t TEST_PRNG_SEED = std::uint64_t{0xC0FFEE1234ULL};
constexpr std::size_t TEST_THREAD_COUNT = 8;
constexpr std::size_t TEST_ITERATION_COUNT = 160;
constexpr std::uint64_t TEST_RANDOM_ACTION_BOUND = std::uint64_t{3};
constexpr std::uint64_t TEST_RANDOM_DELAY_BOUND = std::uint64_t{8};
constexpr mm::AddressValue TEST_STACK_BASE = mm::AddressValue{0x400000};
constexpr mm::AddressValue TEST_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;

[[nodiscard]] std::size_t thread_index(const sched::ThreadContext& thread) noexcept
{
    return static_cast<std::size_t>(thread.id().value() - sched::THREAD_ID_FIRST_KERNEL_VALUE);
}
}

TEST(Stage7SchedulerChaosTest, SleepQueueMatchesDeadlineModel)
{
    mnos::test::DeterministicPrng prng{TEST_PRNG_SEED};
    std::vector<sched::ThreadContext> threads;
    threads.reserve(TEST_THREAD_COUNT);
    for (std::size_t thread_index_value = std::size_t{0}; thread_index_value < TEST_THREAD_COUNT; ++thread_index_value)
    {
        threads.emplace_back(
            sched::ThreadId{
                sched::THREAD_ID_FIRST_KERNEL_VALUE + static_cast<sched::ThreadId::value_type>(thread_index_value)},
            mm::VirtualAddress{
                TEST_STACK_BASE + static_cast<mm::AddressValue>(thread_index_value * TEST_STACK_STRIDE)});
    }

    sched::SleepQueue sleep_queue;
    std::array<bool, TEST_THREAD_COUNT> model_sleeping{};
    std::array<sched::SchedulerTick, TEST_THREAD_COUNT> model_wake_ticks{};

    for (sched::SchedulerTick current_tick = sched::SchedulerTick{0};
         current_tick < static_cast<sched::SchedulerTick>(TEST_ITERATION_COUNT);
         ++current_tick)
    {
        if (prng.next_bounded(TEST_RANDOM_ACTION_BOUND) != std::uint64_t{0})
        {
            const std::size_t candidate_index =
                static_cast<std::size_t>(prng.next_bounded(static_cast<std::uint64_t>(TEST_THREAD_COUNT)));
            if (!model_sleeping[candidate_index])
            {
                const sched::SchedulerTick delay =
                    static_cast<sched::SchedulerTick>(prng.next_bounded(TEST_RANDOM_DELAY_BOUND) + std::uint64_t{1});
                const sched::SchedulerTick wake_tick = current_tick + delay;
                sleep_queue.sleep_until(threads[candidate_index], wake_tick);
                model_sleeping[candidate_index] = true;
                model_wake_ticks[candidate_index] = wake_tick;
            }
        }

        std::array<bool, TEST_THREAD_COUNT> actual_ready{};
        const std::vector<sched::ThreadContext*> ready_threads = sleep_queue.take_ready(current_tick);
        for (sched::ThreadContext* const thread : ready_threads)
        {
            const std::size_t ready_index = thread_index(*thread);
            actual_ready[ready_index] = true;
            thread->set_state(sched::ThreadState::READY);
        }

        std::array<bool, TEST_THREAD_COUNT> expected_ready{};
        std::size_t expected_sleeping_count = std::size_t{0};
        sched::SchedulerTick expected_next_wake_tick = std::numeric_limits<sched::SchedulerTick>::max();
        for (std::size_t model_index = std::size_t{0}; model_index < TEST_THREAD_COUNT; ++model_index)
        {
            if (model_sleeping[model_index] && model_wake_ticks[model_index] <= current_tick)
            {
                expected_ready[model_index] = true;
                model_sleeping[model_index] = false;
            }
            if (model_sleeping[model_index])
            {
                ++expected_sleeping_count;
                if (model_wake_ticks[model_index] < expected_next_wake_tick)
                {
                    expected_next_wake_tick = model_wake_ticks[model_index];
                }
            }
        }

        EXPECT_THAT(actual_ready, Eq(expected_ready));
        EXPECT_THAT(sleep_queue.size(), Eq(expected_sleeping_count));
        if (expected_sleeping_count == std::size_t{0})
        {
            EXPECT_FALSE(sleep_queue.next_wake_tick().has_value());
        }
        else
        {
            ASSERT_TRUE(sleep_queue.next_wake_tick().has_value());
            EXPECT_THAT(sleep_queue.next_wake_tick().value(), Eq(expected_next_wake_tick));
        }
    }
}
