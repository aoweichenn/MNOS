#include <limits>
#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/sched/thread_id.hpp>
#include <mnos/os/sched/thread_state.hpp>

namespace cpu = mnos::cpu;
namespace mm = mnos::os::mm;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_THREAD_STATE = static_cast<sched::ThreadState>(sched::THREAD_STATE_COUNT);
constexpr sched::ThreadId::value_type TEST_THREAD_ID_VALUE = sched::THREAD_ID_FIRST_KERNEL_VALUE;
constexpr mm::AddressValue TEST_STACK_BOTTOM_VALUE = mm::AddressValue{0x8000};
constexpr mm::AddressValue TEST_SECOND_STACK_BOTTOM_VALUE = mm::AddressValue{0xC000};
constexpr mm::AddressValue TEST_UNALIGNED_STACK_BOTTOM_VALUE = TEST_STACK_BOTTOM_VALUE + mm::AddressValue{1};
constexpr std::uint64_t TEST_CUSTOM_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * std::uint64_t{2};
constexpr std::uint64_t TEST_UNALIGNED_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES + std::uint64_t{1};
constexpr cpu::Qword TEST_REGISTER_VALUE = cpu::Qword{0xFEEDBEEFULL};
}

TEST(ThreadIdTest, ProvidesInvalidAndFirstKernelIdSentinels)
{
    constexpr sched::ThreadId invalid_id;
    constexpr sched::ThreadId first_kernel_id = sched::ThreadId::first_kernel_thread();
    constexpr sched::ThreadId second_kernel_id{sched::THREAD_ID_FIRST_KERNEL_VALUE + sched::ThreadId::value_type{1}};

    EXPECT_FALSE(invalid_id.is_valid());
    EXPECT_FALSE(sched::ThreadId::invalid().is_valid());
    EXPECT_TRUE(first_kernel_id.is_valid());
    EXPECT_THAT(first_kernel_id.value(), Eq(TEST_THREAD_ID_VALUE));
    EXPECT_TRUE(first_kernel_id < second_kernel_id);
}

TEST(ThreadStateTest, MapsStatesToStableNames)
{
    EXPECT_TRUE(sched::is_thread_state_valid(sched::ThreadState::READY));
    EXPECT_TRUE(sched::is_thread_state_valid(sched::ThreadState::RUNNING));
    EXPECT_TRUE(sched::is_thread_state_valid(sched::ThreadState::BLOCKED));
    EXPECT_TRUE(sched::is_thread_state_valid(sched::ThreadState::DEAD));
    EXPECT_FALSE(sched::is_thread_state_valid(TEST_INVALID_THREAD_STATE));

    EXPECT_THAT(sched::thread_state_to_index(sched::ThreadState::READY), Eq(std::size_t{0}));
    EXPECT_THAT(sched::thread_state_to_name(sched::ThreadState::RUNNING), Eq(std::string_view{"RUNNING"}));
    EXPECT_THAT(sched::thread_state_to_name(TEST_INVALID_THREAD_STATE), Eq(std::string_view{"<invalid>"}));
}

TEST(ThreadContextTest, InitializesKernelStackAndCpuContext)
{
    sched::ThreadContext thread{sched::ThreadId::first_kernel_thread(), mm::VirtualAddress{TEST_STACK_BOTTOM_VALUE}};
    const sched::ThreadContext& const_thread = thread;

    EXPECT_THAT(thread.id(), Eq(sched::ThreadId::first_kernel_thread()));
    EXPECT_THAT(thread.state(), Eq(sched::ThreadState::READY));
    EXPECT_TRUE(thread.is_runnable());
    EXPECT_TRUE(thread.is_alive());
    EXPECT_THAT(thread.kernel_stack_bottom().value(), Eq(TEST_STACK_BOTTOM_VALUE));
    EXPECT_THAT(thread.kernel_stack_size_bytes(), Eq(sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES));
    EXPECT_THAT(
        thread.kernel_stack_top().value(),
        Eq(TEST_STACK_BOTTOM_VALUE + sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES));
    EXPECT_THAT(
        const_thread.cpu_state().registers().read(cpu::RegisterId::RSP),
        Eq(static_cast<cpu::Qword>(thread.kernel_stack_top().value())));
    EXPECT_TRUE(thread.contains_kernel_stack_address(thread.kernel_stack_bottom()));
    EXPECT_TRUE(thread.contains_kernel_stack_address(thread.kernel_stack_top() - mm::AddressValue{1}));
    EXPECT_FALSE(thread.contains_kernel_stack_address(thread.kernel_stack_top()));
}

TEST(ThreadContextTest, SupportsStateTransitionsAndCpuReset)
{
    sched::ThreadContext thread{
        sched::ThreadId{TEST_THREAD_ID_VALUE},
        mm::VirtualAddress{TEST_SECOND_STACK_BOTTOM_VALUE},
        TEST_CUSTOM_STACK_SIZE_BYTES};

    EXPECT_THAT(thread.kernel_stack_size_bytes(), Eq(TEST_CUSTOM_STACK_SIZE_BYTES));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RSP), Eq(static_cast<cpu::Qword>(thread.kernel_stack_top().value())));

    thread.set_state(sched::ThreadState::RUNNING);
    EXPECT_THAT(thread.state(), Eq(sched::ThreadState::RUNNING));
    EXPECT_FALSE(thread.is_runnable());
    EXPECT_TRUE(thread.is_alive());

    thread.set_state(sched::ThreadState::BLOCKED);
    EXPECT_FALSE(thread.is_runnable());

    thread.set_state(sched::ThreadState::DEAD);
    EXPECT_FALSE(thread.is_alive());

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    thread.cpu_state().halt();
    thread.reset_cpu_state();
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));
    EXPECT_FALSE(thread.cpu_state().is_halted());
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RSP), Eq(static_cast<cpu::Qword>(thread.kernel_stack_top().value())));
}

TEST(ThreadContextTest, RejectsInvalidIdsStacksAndStates)
{
    constexpr mm::AddressValue OVERFLOW_STACK_BOTTOM_VALUE =
        std::numeric_limits<mm::AddressValue>::max() & mm::MM_PAGE_FRAME_MASK;

    EXPECT_THROW(
        static_cast<void>(sched::ThreadContext{sched::ThreadId::invalid(), mm::VirtualAddress{TEST_STACK_BOTTOM_VALUE}}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(
            sched::ThreadContext{sched::ThreadId::first_kernel_thread(), mm::VirtualAddress{TEST_UNALIGNED_STACK_BOTTOM_VALUE}}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(
            sched::ThreadContext{sched::ThreadId::first_kernel_thread(), mm::VirtualAddress{TEST_STACK_BOTTOM_VALUE}, std::uint64_t{0}}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(
            sched::ThreadContext{
                sched::ThreadId::first_kernel_thread(),
                mm::VirtualAddress{TEST_STACK_BOTTOM_VALUE},
                TEST_UNALIGNED_STACK_SIZE_BYTES}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(
            sched::ThreadContext{
                sched::ThreadId::first_kernel_thread(),
                mm::VirtualAddress{OVERFLOW_STACK_BOTTOM_VALUE},
                sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES}),
        std::overflow_error);

    sched::ThreadContext thread{sched::ThreadId::first_kernel_thread(), mm::VirtualAddress{TEST_STACK_BOTTOM_VALUE}};
    EXPECT_THROW(thread.set_state(TEST_INVALID_THREAD_STATE), std::invalid_argument);
}
