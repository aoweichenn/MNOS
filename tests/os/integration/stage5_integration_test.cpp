#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/trap_frame.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{64});
constexpr std::uint32_t TEST_BOOTSTRAP_PROCESSOR_COUNT = std::uint32_t{2};
constexpr mm::VirtualAddress TEST_FAULT_PAGE{mm::AddressValue{0x400000}};
constexpr mm::AddressValue TEST_FAULT_OFFSET = mm::AddressValue{0x40};
constexpr cpu::Qword TEST_USER_NOT_PRESENT_ERROR = cpu_memory::PAGE_FAULT_ERROR_USER_BIT;
constexpr cpu::Qword TEST_QWORD_VALUE = cpu::Qword{0x5152535455565758ULL};

[[nodiscard]] cpu_system::TrapFrame make_user_page_fault_frame()
{
    return cpu_system::TrapFrame{
        cpu_system::TrapKind::EXCEPTION,
        cpu_system::InterruptVector::page_fault(),
        cpu::InstructionPointer{0},
        cpu::InstructionPointer{0},
        cpu::Qword{0},
        cpu::Qword{0},
        cpu_system::PrivilegeLevel::RING3,
        TEST_USER_NOT_PRESENT_ERROR};
}
}

TEST(Stage5IntegrationTest, KernelCreatesProcessSchedulesThreadsAndHandlesPageFaults)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine, TEST_BOOTSTRAP_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    EXPECT_TRUE(os_kernel.has_stage5_services());
    EXPECT_THAT(os_kernel.bootstrap_processor_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));
    EXPECT_FALSE(os_kernel.physical_page_allocator().empty());
    EXPECT_TRUE(os_kernel.kernel_address_space().root_table_address().value() >= mm::MM_PAGE_SIZE_BYTES);

    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& first_thread = os_kernel.create_thread(process);
    sched::ThreadContext& second_thread = os_kernel.create_thread(process);

    EXPECT_THAT(os_kernel.process_count(), Eq(std::size_t{1}));
    EXPECT_THAT(process.thread_count(), Eq(std::size_t{2}));
    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(first_thread.id()));
    ASSERT_NE(os_kernel.scheduler().yield_current(), nullptr);
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(second_thread.id()));

    first_thread.cpu_state().set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    first_thread.cpu_state().paging().set_page_fault_linear_address(mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET));
    first_thread.cpu_state().set_pending_trap(make_user_page_fault_frame());
    EXPECT_THAT(os_kernel.handle_page_fault(process, first_thread), Eq(mm::PageFaultResult::HANDLED));
    EXPECT_FALSE(first_thread.cpu_state().has_pending_trap());

    cpu_memory::MemoryManagementUnit mmu;
    mmu.write(
        machine.memory_bus(),
        first_thread.cpu_state().paging(),
        cpu_system::PrivilegeLevel::RING3,
        mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET),
        cpu::DataSize::QWORD,
        TEST_QWORD_VALUE);
    EXPECT_THAT(
        mmu.read(
            machine.memory_bus(),
            first_thread.cpu_state().paging(),
            cpu_system::PrivilegeLevel::RING3,
            mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET),
            cpu::DataSize::QWORD),
        Eq(TEST_QWORD_VALUE));
}

TEST(Stage5IntegrationTest, KernelDispatchesYieldExitAndUnsupportedSyscalls)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& first_thread = os_kernel.create_thread(process);
    sched::ThreadContext& second_thread = os_kernel.create_thread(process);
    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);

    first_thread.cpu_state().registers().write(
        cpu::RegisterId::RAX,
        static_cast<cpu::Qword>(kernel::SyscallNumber::YIELD));
    EXPECT_THAT(os_kernel.dispatch_syscall(first_thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(first_thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::SYSCALL_SUCCESS_RESULT));
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(second_thread.id()));

    second_thread.cpu_state().registers().write(cpu::RegisterId::RAX, cpu::Qword{99});
    EXPECT_THAT(os_kernel.dispatch_syscall(second_thread), Eq(kernel::SyscallResult::UNSUPPORTED));
    EXPECT_THAT(second_thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::SYSCALL_UNSUPPORTED_RESULT));

    second_thread.cpu_state().registers().write(
        cpu::RegisterId::RAX,
        static_cast<cpu::Qword>(kernel::SyscallNumber::EXIT));
    EXPECT_THAT(os_kernel.dispatch_syscall(second_thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(second_thread.state(), Eq(sched::ThreadState::DEAD));
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(first_thread.id()));
}

TEST(Stage5IntegrationTest, KernelDispatchesSyscallsForThreadOutsideCurrentSlot)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& thread = os_kernel.create_thread(process);

    thread.cpu_state().registers().write(
        cpu::RegisterId::RAX,
        static_cast<cpu::Qword>(kernel::SyscallNumber::YIELD));
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_FALSE(os_kernel.scheduler().has_current());

    thread.cpu_state().registers().write(
        cpu::RegisterId::RAX,
        static_cast<cpu::Qword>(kernel::SyscallNumber::EXIT));
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.state(), Eq(sched::ThreadState::DEAD));
    EXPECT_EQ(os_kernel.scheduler().schedule_next(), nullptr);
}

TEST(Stage5IntegrationTest, KernelHandlesKernelAddressSpacePageFaults)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    sched::ThreadContext thread{sched::ThreadId::first_kernel_thread(), mm::VirtualAddress{0x100000}};
    thread.cpu_state().paging().set_page_fault_linear_address(mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET));
    thread.cpu_state().set_pending_trap(make_user_page_fault_frame());

    EXPECT_THAT(os_kernel.handle_page_fault(thread), Eq(mm::PageFaultResult::HANDLED));
    EXPECT_FALSE(thread.cpu_state().has_pending_trap());

    cpu_memory::MemoryManagementUnit mmu;
    mmu.write(
        machine.memory_bus(),
        thread.cpu_state().paging(),
        cpu_system::PrivilegeLevel::RING3,
        mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET),
        cpu::DataSize::QWORD,
        TEST_QWORD_VALUE);
    EXPECT_THAT(
        mmu.read(
            machine.memory_bus(),
            thread.cpu_state().paging(),
            cpu_system::PrivilegeLevel::RING3,
            mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET),
            cpu::DataSize::QWORD),
        Eq(TEST_QWORD_VALUE));
}
