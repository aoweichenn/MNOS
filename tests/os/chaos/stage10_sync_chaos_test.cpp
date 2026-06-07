#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/proc/futex.hpp>
#include <mnos/os/proc/process_id.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <support/deterministic_prng.hpp>

namespace mm = mnos::os::mm;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;
namespace test = mnos::test;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t CHAOS_STAGE10_SYNC_SEED = std::uint64_t{0x510C'2026ULL};
constexpr std::size_t CHAOS_THREAD_COUNT = 8;
constexpr std::size_t CHAOS_STEP_COUNT = 192;
constexpr std::uint64_t CHAOS_OPERATION_COUNT = std::uint64_t{3};
constexpr mm::AddressValue CHAOS_STACK_BASE = mm::AddressValue{0x4000000};
constexpr mm::AddressValue CHAOS_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;
constexpr proc::ProcessId CHAOS_PROCESS_A{1};
constexpr proc::ProcessId CHAOS_PROCESS_B{2};
constexpr mm::VirtualAddress CHAOS_FUTEX_A = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::VirtualAddress CHAOS_FUTEX_B = mm::ADDRESS_LAYOUT_USER_HEAP_BASE + proc::FUTEX_WORD_ALIGNMENT_BYTES;

[[nodiscard]] sched::ThreadContext make_thread(const std::size_t index)
{
    return sched::ThreadContext{
        sched::ThreadId{sched::THREAD_ID_FIRST_KERNEL_VALUE + static_cast<sched::ThreadId::value_type>(index)},
        mm::VirtualAddress{CHAOS_STACK_BASE + static_cast<mm::AddressValue>(index * CHAOS_STACK_STRIDE)}};
}

[[nodiscard]] std::size_t non_empty_key_count(
    const std::array<std::vector<std::size_t>, 4>& waiters_by_key)
{
    std::size_t count = std::size_t{0};
    for (const std::vector<std::size_t>& waiters : waiters_by_key)
    {
        if (!waiters.empty())
        {
            ++count;
        }
    }
    return count;
}
}

TEST(Stage10SyncChaosTest, RandomFutexMutationsPreserveBucketAndWaiterInvariants)
{
    test::DeterministicPrng prng{CHAOS_STAGE10_SYNC_SEED};
    proc::FutexTable futex_table;
    const std::array<proc::FutexKey, 4> keys{
        proc::FutexKey{CHAOS_PROCESS_A, CHAOS_FUTEX_A},
        proc::FutexKey{CHAOS_PROCESS_A, CHAOS_FUTEX_B},
        proc::FutexKey{CHAOS_PROCESS_B, CHAOS_FUTEX_A},
        proc::FutexKey{CHAOS_PROCESS_B, CHAOS_FUTEX_B}};
    std::vector<sched::ThreadContext> threads;
    threads.reserve(CHAOS_THREAD_COUNT);
    std::vector<std::optional<std::size_t>> waiting_key_by_thread(CHAOS_THREAD_COUNT, std::nullopt);
    std::array<std::vector<std::size_t>, 4> waiters_by_key;

    for (std::size_t thread_index = std::size_t{0}; thread_index < CHAOS_THREAD_COUNT; ++thread_index)
    {
        threads.push_back(make_thread(thread_index));
    }

    for (std::size_t step_index = std::size_t{0}; step_index < CHAOS_STEP_COUNT; ++step_index)
    {
        const std::uint64_t operation = prng.next_bounded(CHAOS_OPERATION_COUNT);
        const std::size_t key_index = static_cast<std::size_t>(prng.next_bounded(keys.size()));
        if (operation == std::uint64_t{0})
        {
            const std::size_t thread_index = static_cast<std::size_t>(prng.next_bounded(CHAOS_THREAD_COUNT));
            if (!waiting_key_by_thread[thread_index].has_value())
            {
                futex_table.wait(keys[key_index], threads[thread_index]);
                waiting_key_by_thread[thread_index] = key_index;
                waiters_by_key[key_index].push_back(thread_index);
                EXPECT_THAT(threads[thread_index].state(), Eq(sched::ThreadState::BLOCKED));
            }
        }
        else if (operation == std::uint64_t{1})
        {
            const bool has_expected_thread = !waiters_by_key[key_index].empty();
            sched::ThreadContext* const woken_thread = futex_table.wake_one(keys[key_index]);
            if (!has_expected_thread)
            {
                EXPECT_EQ(woken_thread, nullptr);
            }
            else
            {
                const std::size_t expected_thread = waiters_by_key[key_index].front();
                waiters_by_key[key_index].erase(waiters_by_key[key_index].begin());
                ASSERT_NE(woken_thread, nullptr);
                EXPECT_EQ(woken_thread, &threads[expected_thread]);
                waiting_key_by_thread[expected_thread].reset();
                woken_thread->set_state(sched::ThreadState::READY);
            }
        }
        else
        {
            const std::vector<std::size_t> expected_waiters = waiters_by_key[key_index];
            const std::vector<sched::ThreadContext*> woken_threads = futex_table.wake_all(keys[key_index]);
            ASSERT_THAT(woken_threads.size(), Eq(expected_waiters.size()));
            waiters_by_key[key_index].clear();
            for (std::size_t waiter_index = std::size_t{0}; waiter_index < expected_waiters.size(); ++waiter_index)
            {
                sched::ThreadContext* const thread = woken_threads[waiter_index];
                const std::size_t thread_index = expected_waiters[waiter_index];
                ASSERT_NE(thread, nullptr);
                EXPECT_EQ(thread, &threads[thread_index]);
                EXPECT_TRUE(waiting_key_by_thread[thread_index].has_value());
                EXPECT_THAT(waiting_key_by_thread[thread_index].value(), Eq(key_index));
                waiting_key_by_thread[thread_index].reset();
                thread->set_state(sched::ThreadState::READY);
            }
        }

        EXPECT_THAT(futex_table.futex_count(), Eq(non_empty_key_count(waiters_by_key)));
        for (std::size_t checked_key_index = std::size_t{0}; checked_key_index < keys.size(); ++checked_key_index)
        {
            EXPECT_THAT(
                futex_table.waiter_count(keys[checked_key_index]),
                Eq(waiters_by_key[checked_key_index].size()));
        }
        for (std::size_t thread_index = std::size_t{0}; thread_index < threads.size(); ++thread_index)
        {
            for (std::size_t checked_key_index = std::size_t{0}; checked_key_index < keys.size(); ++checked_key_index)
            {
                EXPECT_THAT(
                    futex_table.contains(keys[checked_key_index], threads[thread_index]),
                    Eq(waiting_key_by_thread[thread_index].has_value() &&
                        waiting_key_by_thread[thread_index].value() == checked_key_index));
            }
        }
    }
}
