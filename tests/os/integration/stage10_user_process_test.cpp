#include <array>
#include <cstddef>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/trap_frame.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/copy_on_write.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/user_loader.hpp>
#include <mnos/os/sched/thread_context.hpp>

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

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{256});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr mm::VirtualAddress TEST_TEXT_BASE = mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mm::VirtualAddress TEST_DATA_BASE = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::AddressValue TEST_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr cpu::Byte TEST_TEXT_BYTE = cpu::Byte{0xC3};
constexpr cpu::Byte TEST_DATA_BYTE = cpu::Byte{0x7F};
constexpr cpu::Qword TEST_PARENT_VALUE = cpu::Qword{0x0102030405060708ULL};
constexpr cpu::Qword TEST_CHILD_VALUE = cpu::Qword{0x8877665544332211ULL};

[[nodiscard]] proc::UserProgram make_program()
{
    proc::UserProgram program{TEST_TEXT_BASE};
    program.set_initial_stack_size_bytes(TEST_STACK_SIZE_BYTES);
    program.add_segment(proc::UserSegment::text(TEST_TEXT_BASE, std::vector<cpu::Byte>{TEST_TEXT_BYTE}));
    program.add_segment(proc::UserSegment::data(TEST_DATA_BASE, std::vector<cpu::Byte>{TEST_DATA_BYTE}));
    return program;
}

[[nodiscard]] cpu_system::TrapFrame make_user_write_fault_frame()
{
    return cpu_system::TrapFrame{
        cpu_system::TrapKind::EXCEPTION,
        cpu_system::InterruptVector::page_fault(),
        cpu::InstructionPointer{0},
        cpu::InstructionPointer{0},
        cpu::Qword{0},
        cpu::Qword{0},
        cpu_system::PrivilegeLevel::RING3,
        cpu_memory::PAGE_FAULT_ERROR_PRESENT_BIT |
            cpu_memory::PAGE_FAULT_ERROR_WRITE_BIT |
            cpu_memory::PAGE_FAULT_ERROR_USER_BIT};
}

void arm_child_cow_fault(proc::Process& process, sched::ThreadContext& thread)
{
    process.address_space().activate(
        thread.cpu_state(),
        cpu_memory::ProcessContextId{static_cast<cpu_memory::ProcessContextId::value_type>(process.id().value())},
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    thread.cpu_state().set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    thread.cpu_state().paging().set_page_fault_linear_address(mm::to_cpu_address(TEST_DATA_BASE));
    thread.cpu_state().set_pending_trap(make_user_write_fault_frame());
}
}

TEST(Stage10UserProcessIntegrationTest, KernelLoadsUserProcessForksCowAndWakesFutex)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    ASSERT_TRUE(os_kernel.has_stage10_services());

    proc::UserProgram program = make_program();
    proc::Process& parent = os_kernel.create_user_process(program);
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{1}));
    ASSERT_THAT(parent.thread_count(), Eq(std::size_t{1}));
    EXPECT_THAT(os_kernel.scheduler().ready_count(), Eq(std::size_t{1}));

    const cpu_memory::PageTranslation parent_data_before =
        parent.address_space().page_translation(TEST_DATA_BASE, cpu_memory::MemoryAccessKind::WRITE, cpu_system::PrivilegeLevel::RING3);
    machine.memory_bus().write(parent_data_before.physical_frame_base(), cpu::DataSize::QWORD, TEST_PARENT_VALUE);

    const std::array<mm::VirtualAddress, 1> cow_pages{TEST_DATA_BASE};
    proc::Process& child = os_kernel.fork_process_cow(parent, cow_pages);
    EXPECT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    EXPECT_THAT(os_kernel.copy_on_write_manager().mapping_count(), Eq(std::size_t{2}));
    EXPECT_FALSE(parent.address_space().page_translation(TEST_DATA_BASE).permissions().writable());
    EXPECT_FALSE(child.address_space().page_translation(TEST_DATA_BASE).permissions().writable());

    sched::ThreadContext& child_thread = os_kernel.create_thread(child);
    arm_child_cow_fault(child, child_thread);
    EXPECT_THAT(os_kernel.handle_cow_write_fault(child, child_thread), Eq(proc::CowFaultResult::COPIED));
    EXPECT_FALSE(child_thread.cpu_state().has_pending_trap());
    EXPECT_THAT(os_kernel.copy_on_write_manager().mapping_count(), Eq(std::size_t{1}));

    const cpu_memory::PageTranslation child_data_after =
        child.address_space().page_translation(TEST_DATA_BASE, cpu_memory::MemoryAccessKind::WRITE, cpu_system::PrivilegeLevel::RING3);
    EXPECT_FALSE(child_data_after.physical_frame_base() == parent_data_before.physical_frame_base());
    EXPECT_TRUE(child_data_after.permissions().writable());

    cpu_memory::MemoryManagementUnit mmu;
    mmu.write(
        machine.memory_bus(),
        child_thread.cpu_state().paging(),
        cpu_system::PrivilegeLevel::RING3,
        mm::to_cpu_address(TEST_DATA_BASE),
        cpu::DataSize::QWORD,
        TEST_CHILD_VALUE);
    EXPECT_THAT(machine.memory_bus().read(parent_data_before.physical_frame_base(), cpu::DataSize::QWORD), Eq(TEST_PARENT_VALUE));
    EXPECT_THAT(machine.memory_bus().read(child_data_after.physical_frame_base(), cpu::DataSize::QWORD), Eq(TEST_CHILD_VALUE));

    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);
    sched::ThreadContext& current_thread = os_kernel.scheduler().current();
    static_cast<void>(os_kernel.wait_on_futex(parent, TEST_DATA_BASE, current_thread));
    EXPECT_THAT(current_thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_THAT(os_kernel.futex_table().waiter_count(proc::FutexKey{parent.id(), TEST_DATA_BASE}), Eq(std::size_t{1}));

    sched::ThreadContext* const woken_thread = os_kernel.wake_one_futex(parent, TEST_DATA_BASE);
    ASSERT_NE(woken_thread, nullptr);
    EXPECT_EQ(woken_thread, &current_thread);
    EXPECT_THAT(woken_thread->state(), Eq(sched::ThreadState::READY));
    EXPECT_THAT(os_kernel.futex_table().waiter_count(proc::FutexKey{parent.id(), TEST_DATA_BASE}), Eq(std::size_t{0}));
}
