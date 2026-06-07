#include <array>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <support/deterministic_prng.hpp>

namespace cpu = mnos::cpu;
namespace mm = mnos::os::mm;
namespace sched = mnos::os::sched;
namespace test = mnos::test;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t CHAOS_SEED = 0x05C0FFEEULL;
constexpr std::size_t CHAOS_THREAD_COUNT = 16;
constexpr std::size_t CHAOS_MUTATION_COUNT = 256;
constexpr mm::AddressValue CHAOS_STACK_REGION_BASE = mm::AddressValue{0x100000};
constexpr mm::AddressValue CHAOS_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;

enum class ChaosOperation : std::uint8_t
{
    SET_READY,
    SET_RUNNING,
    SET_BLOCKED,
    SET_DEAD,
    RESET_CPU,
    WRITE_RAX,
    COUNT
};

inline constexpr std::uint64_t CHAOS_OPERATION_VARIANT_COUNT = static_cast<std::uint64_t>(ChaosOperation::COUNT);

[[nodiscard]] std::size_t next_thread_index(test::DeterministicPrng& prng) noexcept
{
    return static_cast<std::size_t>(prng.next_bounded(CHAOS_THREAD_COUNT));
}
}

TEST(ThreadContextChaosTest, RandomStateAndCpuMutationsStayIsolated)
{
    test::DeterministicPrng prng{CHAOS_SEED};
    std::vector<sched::ThreadContext> threads;
    threads.reserve(CHAOS_THREAD_COUNT);
    std::array<sched::ThreadState, CHAOS_THREAD_COUNT> expected_states{};
    std::array<cpu::Qword, CHAOS_THREAD_COUNT> expected_rax{};

    for (std::size_t thread_index = 0; thread_index < CHAOS_THREAD_COUNT; ++thread_index)
    {
        const auto id_value =
            static_cast<sched::ThreadId::value_type>(sched::THREAD_ID_FIRST_KERNEL_VALUE + thread_index);
        const mm::AddressValue stack_bottom = CHAOS_STACK_REGION_BASE + (thread_index * CHAOS_STACK_STRIDE);
        threads.emplace_back(sched::ThreadId{id_value}, mm::VirtualAddress{stack_bottom});
        expected_states[thread_index] = sched::ThreadState::READY;
    }

    for (std::size_t operation_index = 0; operation_index < CHAOS_MUTATION_COUNT; ++operation_index)
    {
        const std::size_t thread_index = next_thread_index(prng);
        sched::ThreadContext& thread = threads[thread_index];
        const auto operation = static_cast<ChaosOperation>(prng.next_bounded(CHAOS_OPERATION_VARIANT_COUNT));

        switch (operation)
        {
        case ChaosOperation::SET_READY:
            thread.set_state(sched::ThreadState::READY);
            expected_states[thread_index] = sched::ThreadState::READY;
            break;
        case ChaosOperation::SET_RUNNING:
            thread.set_state(sched::ThreadState::RUNNING);
            expected_states[thread_index] = sched::ThreadState::RUNNING;
            break;
        case ChaosOperation::SET_BLOCKED:
            thread.set_state(sched::ThreadState::BLOCKED);
            expected_states[thread_index] = sched::ThreadState::BLOCKED;
            break;
        case ChaosOperation::SET_DEAD:
            thread.set_state(sched::ThreadState::DEAD);
            expected_states[thread_index] = sched::ThreadState::DEAD;
            break;
        case ChaosOperation::RESET_CPU:
            thread.reset_cpu_state();
            expected_rax[thread_index] = cpu::Qword{0};
            break;
        case ChaosOperation::WRITE_RAX:
        {
            const cpu::Qword value = prng.next();
            thread.cpu_state().registers().write(cpu::RegisterId::RAX, value);
            expected_rax[thread_index] = value;
            break;
        }
        case ChaosOperation::COUNT:
            throw std::logic_error{"thread chaos sentinel should not be generated"};
        }
    }

    for (std::size_t thread_index = 0; thread_index < CHAOS_THREAD_COUNT; ++thread_index)
    {
        const sched::ThreadContext& thread = threads[thread_index];
        EXPECT_THAT(thread.state(), Eq(expected_states[thread_index]));
        EXPECT_THAT(thread.is_runnable(), Eq(expected_states[thread_index] == sched::ThreadState::READY));
        EXPECT_THAT(thread.is_alive(), Eq(expected_states[thread_index] != sched::ThreadState::DEAD));
        EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(expected_rax[thread_index]));
        EXPECT_THAT(
            thread.cpu_state().registers().read(cpu::RegisterId::RSP),
            Eq(static_cast<cpu::Qword>(thread.kernel_stack_top().value())));
    }
}
