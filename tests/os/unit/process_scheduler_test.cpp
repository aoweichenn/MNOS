#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/syscall.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/process_id.hpp>
#include <mnos/os/sched/round_robin_scheduler.hpp>

namespace cpu = mnos::cpu;
namespace io = mnos::os::io;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{32});
constexpr mm::PhysicalAddress TEST_ROOT_TABLE{mm::AddressValue{0x1000}};
constexpr mm::PhysicalAddress TEST_NEXT_TABLE{mm::AddressValue{0x2000}};
constexpr mm::PhysicalAddress TEST_TABLE_ARENA_END{mm::AddressValue{0x6000}};
constexpr mm::VirtualAddress TEST_STACK_BOTTOM{mm::AddressValue{0x800000}};
constexpr mm::VirtualAddress TEST_SECOND_STACK_BOTTOM{mm::AddressValue{0x900000}};
constexpr mm::VirtualAddress TEST_THIRD_STACK_BOTTOM{mm::AddressValue{0xA00000}};

[[nodiscard]] mm::AddressSpace make_address_space(platform::Machine& machine)
{
    return mm::AddressSpace{machine.memory_bus(), TEST_ROOT_TABLE, TEST_NEXT_TABLE, TEST_TABLE_ARENA_END};
}
}

TEST(ProcessTest, OwnsAddressSpaceAndThreadContexts)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    proc::Process process{proc::ProcessId::first_user_process(), make_address_space(machine)};
    const proc::Process& const_process = process;

    EXPECT_THAT(process.id(), Eq(proc::ProcessId::first_user_process()));
    EXPECT_TRUE(process.empty());
    EXPECT_TRUE(const_process.file_descriptors().readable(io::FileDescriptor::stdin()));
    EXPECT_TRUE(const_process.file_descriptors().writable(io::FileDescriptor::stdout()));
    sched::ThreadContext& thread = process.create_thread(sched::ThreadId::first_kernel_thread(), TEST_STACK_BOTTOM);
    EXPECT_FALSE(process.empty());
    EXPECT_THAT(process.thread_count(), Eq(std::size_t{1}));
    EXPECT_THAT(const_process.address_space().root_table_address(), Eq(TEST_ROOT_TABLE));
    EXPECT_THAT(process.thread_at(std::size_t{0}).id(), Eq(thread.id()));
    EXPECT_THAT(const_process.thread_at(std::size_t{0}).kernel_stack_bottom(), Eq(TEST_STACK_BOTTOM));
    EXPECT_THROW(static_cast<void>(process.create_thread(thread.id(), TEST_SECOND_STACK_BOTTOM)), std::logic_error);
    EXPECT_THROW(static_cast<void>(process.thread_at(std::size_t{1})), std::out_of_range);
    EXPECT_THROW(static_cast<void>(const_process.thread_at(std::size_t{1})), std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(proc::Process{proc::ProcessId::invalid(), make_address_space(machine)}),
        std::invalid_argument);
}

TEST(SyscallTest, MapsNumbersAndNames)
{
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::YIELD));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::EXIT));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::READ));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::WRITE));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::OPEN));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::READDIR));
    EXPECT_FALSE(kernel::is_syscall_number_valid(kernel::SyscallNumber::COUNT));
    EXPECT_THAT(kernel::syscall_number_to_index(kernel::SyscallNumber::EXIT), Eq(std::size_t{1}));
    EXPECT_THAT(kernel::syscall_number_to_name(kernel::SyscallNumber::YIELD), Eq(std::string_view{"YIELD"}));
    EXPECT_THAT(kernel::syscall_number_to_name(kernel::SyscallNumber::READ), Eq(std::string_view{"READ"}));
    EXPECT_THAT(kernel::syscall_number_to_name(kernel::SyscallNumber::WRITE), Eq(std::string_view{"WRITE"}));
    EXPECT_THAT(kernel::syscall_number_to_name(kernel::SyscallNumber::OPEN), Eq(std::string_view{"OPEN"}));
    EXPECT_THAT(kernel::syscall_number_to_name(kernel::SyscallNumber::READDIR), Eq(std::string_view{"READDIR"}));
    EXPECT_THAT(kernel::syscall_number_to_name(kernel::SyscallNumber::COUNT), Eq(std::string_view{"<invalid>"}));
    EXPECT_THAT(kernel::syscall_number_from_raw(cpu::Qword{0}), Eq(kernel::SyscallNumber::YIELD));
    EXPECT_THAT(kernel::syscall_number_from_raw(cpu::Qword{9}), Eq(kernel::SyscallNumber::WRITE));
    EXPECT_THAT(kernel::syscall_number_from_raw(cpu::Qword{10}), Eq(kernel::SyscallNumber::OPEN));
    EXPECT_THAT(kernel::syscall_number_from_raw(cpu::Qword{13}), Eq(kernel::SyscallNumber::READDIR));
    EXPECT_THAT(kernel::syscall_number_from_raw(cpu::Qword{14}), Eq(kernel::SyscallNumber::COUNT));
}

TEST(RoundRobinSchedulerTest, SchedulesBlocksWakesAndExitsThreads)
{
    sched::ThreadContext first_thread{sched::ThreadId{1}, TEST_STACK_BOTTOM};
    sched::ThreadContext second_thread{sched::ThreadId{2}, TEST_SECOND_STACK_BOTTOM};
    sched::ThreadContext third_thread{sched::ThreadId{3}, TEST_THIRD_STACK_BOTTOM};
    sched::RoundRobinScheduler scheduler;

    EXPECT_TRUE(scheduler.empty());
    EXPECT_THROW(static_cast<void>(scheduler.current()), std::logic_error);

    scheduler.enqueue(first_thread);
    scheduler.enqueue(second_thread);
    EXPECT_THAT(scheduler.ready_count(), Eq(std::size_t{2}));
    EXPECT_THROW(scheduler.enqueue(first_thread), std::logic_error);

    ASSERT_NE(scheduler.schedule_next(), nullptr);
    EXPECT_THAT(scheduler.current().id(), Eq(first_thread.id()));
    EXPECT_THAT(first_thread.state(), Eq(sched::ThreadState::RUNNING));

    ASSERT_NE(scheduler.yield_current(), nullptr);
    EXPECT_THAT(scheduler.current().id(), Eq(second_thread.id()));
    EXPECT_THAT(first_thread.state(), Eq(sched::ThreadState::READY));

    ASSERT_NE(scheduler.block_current(), nullptr);
    EXPECT_THAT(second_thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_THAT(scheduler.current().id(), Eq(first_thread.id()));

    scheduler.wake(second_thread);
    ASSERT_NE(scheduler.yield_current(), nullptr);
    EXPECT_THAT(scheduler.current().id(), Eq(second_thread.id()));

    third_thread.set_state(sched::ThreadState::DEAD);
    EXPECT_THROW(scheduler.enqueue(third_thread), std::logic_error);
    ASSERT_NE(scheduler.exit_current(), nullptr);
    EXPECT_THAT(second_thread.state(), Eq(sched::ThreadState::DEAD));
    EXPECT_THAT(scheduler.current().id(), Eq(first_thread.id()));
}

TEST(RoundRobinSchedulerTest, HandlesNoCurrentConstCurrentAndWakeErrors)
{
    sched::ThreadContext first_thread{sched::ThreadId{1}, TEST_STACK_BOTTOM};
    sched::ThreadContext second_thread{sched::ThreadId{2}, TEST_SECOND_STACK_BOTTOM};
    sched::RoundRobinScheduler scheduler;
    const sched::RoundRobinScheduler& const_scheduler = scheduler;

    EXPECT_EQ(scheduler.block_current(), nullptr);
    EXPECT_EQ(scheduler.exit_current(), nullptr);
    EXPECT_THROW(static_cast<void>(const_scheduler.current()), std::logic_error);

    scheduler.enqueue(first_thread);
    ASSERT_NE(scheduler.schedule_next(), nullptr);
    EXPECT_THAT(const_scheduler.current().id(), Eq(first_thread.id()));
    EXPECT_THROW(scheduler.wake(first_thread), std::logic_error);

    second_thread.set_state(sched::ThreadState::DEAD);
    EXPECT_THROW(scheduler.wake(second_thread), std::logic_error);
}

TEST(RoundRobinSchedulerTest, SkipsBlockedQueuedThreads)
{
    sched::ThreadContext blocked_thread{sched::ThreadId{1}, TEST_STACK_BOTTOM};
    sched::RoundRobinScheduler scheduler;

    scheduler.enqueue(blocked_thread);
    blocked_thread.set_state(sched::ThreadState::BLOCKED);

    EXPECT_EQ(scheduler.schedule_next(), nullptr);
    EXPECT_FALSE(scheduler.has_current());
    EXPECT_EQ(scheduler.yield_current(), nullptr);
}
