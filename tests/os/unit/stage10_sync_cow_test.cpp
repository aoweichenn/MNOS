#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/trap_frame.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/copy_on_write.hpp>
#include <mnos/os/proc/futex.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/event.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{128});
constexpr mm::VirtualAddress TEST_COW_PAGE = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::VirtualAddress TEST_FUTEX_ADDRESS = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::PhysicalAddress TEST_PARENT_ROOT{mm::AddressValue{0x1000}};
constexpr mm::PhysicalAddress TEST_PARENT_NEXT{mm::AddressValue{0x2000}};
constexpr mm::PhysicalAddress TEST_PARENT_END{mm::AddressValue{0x8000}};
constexpr mm::PhysicalAddress TEST_CHILD_ROOT{mm::AddressValue{0x8000}};
constexpr mm::PhysicalAddress TEST_CHILD_NEXT{mm::AddressValue{0x9000}};
constexpr mm::PhysicalAddress TEST_CHILD_END{mm::AddressValue{0xF000}};
constexpr mm::PhysicalAddress TEST_GRANDCHILD_ROOT{mm::AddressValue{0xF000}};
constexpr mm::PhysicalAddress TEST_GRANDCHILD_NEXT{mm::AddressValue{0x10000}};
constexpr mm::PhysicalAddress TEST_GRANDCHILD_END{mm::AddressValue{0x16000}};
constexpr mm::PhysicalAddress TEST_COW_FRAME{mm::AddressValue{0x20000}};
constexpr mm::PhysicalAddress TEST_FIRST_ALLOCATED_COPY{mm::AddressValue{0x30000}};
constexpr mm::VirtualAddress TEST_STACK_BASE{mm::AddressValue{0x900000}};
constexpr mm::AddressValue TEST_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;
constexpr proc::ProcessId TEST_PARENT_ID{1};
constexpr proc::ProcessId TEST_CHILD_ID{2};
constexpr proc::ProcessId TEST_GRANDCHILD_ID{3};
constexpr cpu::Qword TEST_ORIGINAL_VALUE = cpu::Qword{0xCAFEBABE01020304ULL};
constexpr cpu::Qword TEST_CHILD_VALUE = cpu::Qword{0x1020304050607080ULL};

[[nodiscard]] mm::AddressSpace make_address_space(
    platform::Machine& machine,
    const mm::PhysicalAddress root_table,
    const mm::PhysicalAddress next_table,
    const mm::PhysicalAddress arena_end)
{
    return mm::AddressSpace{machine.memory_bus(), root_table, next_table, arena_end};
}

[[nodiscard]] sched::ThreadContext make_thread(const sched::ThreadId::value_type thread_id)
{
    return sched::ThreadContext{
        sched::ThreadId{thread_id},
        TEST_STACK_BASE + (static_cast<mm::AddressValue>(thread_id) * TEST_STACK_STRIDE)};
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

void arm_cow_fault(cpu::CpuState& cpu_state)
{
    cpu_state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    cpu_state.paging().set_page_fault_linear_address(mm::to_cpu_address(TEST_COW_PAGE));
    cpu_state.set_pending_trap(make_user_write_fault_frame());
}
}

TEST(Stage10EventFutexTest, EventWaitSignalAndManualResetSemantics)
{
    sched::ThreadContext first_thread = make_thread(sched::ThreadId::value_type{1});
    sched::ThreadContext second_thread = make_thread(sched::ThreadId::value_type{2});
    sched::Event event;

    EXPECT_FALSE(event.is_signaled());
    EXPECT_TRUE(event.wait(first_thread));
    EXPECT_THAT(first_thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_TRUE(event.contains(first_thread));
    EXPECT_THROW(static_cast<void>(event.wait(first_thread)), std::logic_error);

    EXPECT_EQ(event.signal_one(), &first_thread);
    EXPECT_TRUE(event.is_signaled());
    EXPECT_FALSE(event.contains(first_thread));
    EXPECT_THAT(event.waiter_count(), Eq(std::size_t{0}));

    EXPECT_FALSE(event.wait(second_thread));
    EXPECT_THAT(second_thread.state(), Eq(sched::ThreadState::READY));

    event.reset();
    EXPECT_FALSE(event.is_signaled());
    EXPECT_TRUE(event.wait(second_thread));
    const std::vector<sched::ThreadContext*> ready_threads = event.signal_all();
    ASSERT_THAT(ready_threads.size(), Eq(std::size_t{1}));
    EXPECT_EQ(ready_threads.front(), &second_thread);
    EXPECT_THAT(event.waiter_count(), Eq(std::size_t{0}));
}

TEST(Stage10EventFutexTest, FutexTableSeparatesProcessScopedWordKeys)
{
    sched::ThreadContext first_thread = make_thread(sched::ThreadId::value_type{1});
    sched::ThreadContext second_thread = make_thread(sched::ThreadId::value_type{2});
    proc::FutexTable futex_table;
    const proc::FutexKey parent_key{TEST_PARENT_ID, TEST_FUTEX_ADDRESS};
    const proc::FutexKey child_key{TEST_CHILD_ID, TEST_FUTEX_ADDRESS};

    EXPECT_TRUE(futex_table.empty());
    EXPECT_THROW(static_cast<void>(proc::FutexKey{proc::ProcessId::invalid(), TEST_FUTEX_ADDRESS}), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(proc::FutexKey{TEST_PARENT_ID, mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE}), std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(proc::FutexKey{TEST_PARENT_ID, TEST_FUTEX_ADDRESS + mm::AddressValue{1}}),
        std::invalid_argument);

    futex_table.wait(parent_key, first_thread);
    futex_table.wait(child_key, second_thread);
    EXPECT_THAT(futex_table.futex_count(), Eq(std::size_t{2}));
    EXPECT_THAT(futex_table.waiter_count(parent_key), Eq(std::size_t{1}));
    EXPECT_THAT(futex_table.waiter_count(child_key), Eq(std::size_t{1}));
    EXPECT_TRUE(futex_table.contains(parent_key, first_thread));
    EXPECT_THROW(futex_table.wait(parent_key, first_thread), std::logic_error);

    EXPECT_EQ(futex_table.wake_one(parent_key), &first_thread);
    EXPECT_THAT(futex_table.waiter_count(parent_key), Eq(std::size_t{0}));
    EXPECT_THAT(futex_table.futex_count(), Eq(std::size_t{1}));
    EXPECT_EQ(futex_table.wake_one(parent_key), nullptr);

    const std::vector<sched::ThreadContext*> child_waiters = futex_table.wake_all(child_key);
    ASSERT_THAT(child_waiters.size(), Eq(std::size_t{1}));
    EXPECT_EQ(child_waiters.front(), &second_thread);
    EXPECT_TRUE(futex_table.empty());
}

TEST(Stage10CopyOnWriteTest, SharesWritablePageAndResolvesCopyThenRestore)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{128}, mm::PageNumber{48}};
    proc::Process parent{
        TEST_PARENT_ID,
        make_address_space(machine, TEST_PARENT_ROOT, TEST_PARENT_NEXT, TEST_PARENT_END)};
    proc::Process child{
        TEST_CHILD_ID,
        make_address_space(machine, TEST_CHILD_ROOT, TEST_CHILD_NEXT, TEST_CHILD_END)};
    proc::CopyOnWriteManager cow_manager;
    const std::array<mm::VirtualAddress, 1> cow_pages{TEST_COW_PAGE};

    parent.address_space().map_page(
        TEST_COW_PAGE,
        TEST_COW_FRAME,
        cpu_memory::PagePermissions::user_read_write_no_execute());
    machine.memory_bus().write(mm::to_cpu_address(TEST_COW_FRAME), cpu::DataSize::QWORD, TEST_ORIGINAL_VALUE);

    EXPECT_THAT(cow_manager.share_pages(parent, child, cow_pages), Eq(std::size_t{1}));
    EXPECT_THAT(cow_manager.mapping_count(), Eq(std::size_t{2}));
    EXPECT_THAT(cow_manager.frame_ref_count(TEST_COW_FRAME), Eq(std::size_t{2}));
    EXPECT_FALSE(parent.address_space().page_translation(TEST_COW_PAGE).permissions().writable());
    EXPECT_FALSE(child.address_space().page_translation(TEST_COW_PAGE).permissions().writable());

    cpu::CpuState child_state;
    child.address_space().activate(
        child_state,
        cpu_memory::ProcessContextId{static_cast<cpu_memory::ProcessContextId::value_type>(TEST_CHILD_ID.value())},
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    arm_cow_fault(child_state);
    EXPECT_THAT(
        cow_manager.resolve_write_fault(allocator, machine.memory_bus(), child, child_state),
        Eq(proc::CowFaultResult::COPIED));
    EXPECT_FALSE(child_state.has_pending_trap());
    EXPECT_THAT(cow_manager.mapping_count(), Eq(std::size_t{1}));
    EXPECT_THAT(cow_manager.frame_ref_count(TEST_COW_FRAME), Eq(std::size_t{1}));

    const cpu_memory::PageTranslation child_translation = child.address_space().page_translation(TEST_COW_PAGE);
    EXPECT_THAT(child_translation.physical_frame_base(), Eq(mm::to_cpu_address(TEST_FIRST_ALLOCATED_COPY)));
    EXPECT_TRUE(child_translation.permissions().writable());

    cpu_memory::MemoryManagementUnit mmu;
    mmu.write(
        machine.memory_bus(),
        child_state.paging(),
        cpu_system::PrivilegeLevel::RING3,
        mm::to_cpu_address(TEST_COW_PAGE),
        cpu::DataSize::QWORD,
        TEST_CHILD_VALUE);
    EXPECT_THAT(machine.memory_bus().read(mm::to_cpu_address(TEST_COW_FRAME), cpu::DataSize::QWORD), Eq(TEST_ORIGINAL_VALUE));
    EXPECT_THAT(machine.memory_bus().read(mm::to_cpu_address(TEST_FIRST_ALLOCATED_COPY), cpu::DataSize::QWORD), Eq(TEST_CHILD_VALUE));

    cpu::CpuState parent_state;
    parent.address_space().activate(
        parent_state,
        cpu_memory::ProcessContextId{static_cast<cpu_memory::ProcessContextId::value_type>(TEST_PARENT_ID.value())},
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    arm_cow_fault(parent_state);
    EXPECT_THAT(
        cow_manager.resolve_write_fault(allocator, machine.memory_bus(), parent, parent_state),
        Eq(proc::CowFaultResult::RESTORED));
    EXPECT_FALSE(parent_state.has_pending_trap());
    EXPECT_THAT(cow_manager.mapping_count(), Eq(std::size_t{0}));
    EXPECT_THAT(cow_manager.frame_ref_count(TEST_COW_FRAME), Eq(std::size_t{0}));
    EXPECT_TRUE(parent.address_space().page_translation(TEST_COW_PAGE).permissions().writable());
}

TEST(Stage10CopyOnWriteTest, ForksAlreadyCowParentWithoutDuplicatingParentMapping)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    proc::Process parent{
        TEST_PARENT_ID,
        make_address_space(machine, TEST_PARENT_ROOT, TEST_PARENT_NEXT, TEST_PARENT_END)};
    proc::Process first_child{
        TEST_CHILD_ID,
        make_address_space(machine, TEST_CHILD_ROOT, TEST_CHILD_NEXT, TEST_CHILD_END)};
    proc::Process second_child{
        TEST_GRANDCHILD_ID,
        make_address_space(machine, TEST_GRANDCHILD_ROOT, TEST_GRANDCHILD_NEXT, TEST_GRANDCHILD_END)};
    proc::CopyOnWriteManager cow_manager;
    const std::array<mm::VirtualAddress, 1> cow_pages{TEST_COW_PAGE};

    parent.address_space().map_page(
        TEST_COW_PAGE,
        TEST_COW_FRAME,
        cpu_memory::PagePermissions::user_read_write_no_execute());

    EXPECT_THAT(cow_manager.share_pages(parent, first_child, cow_pages), Eq(std::size_t{1}));
    EXPECT_THAT(cow_manager.share_pages(parent, second_child, cow_pages), Eq(std::size_t{1}));
    EXPECT_THAT(cow_manager.mapping_count(), Eq(std::size_t{3}));
    EXPECT_THAT(cow_manager.frame_ref_count(TEST_COW_FRAME), Eq(std::size_t{3}));
    EXPECT_TRUE(cow_manager.contains(TEST_PARENT_ID, TEST_COW_PAGE));
    EXPECT_TRUE(cow_manager.contains(TEST_CHILD_ID, TEST_COW_PAGE));
    EXPECT_TRUE(cow_manager.contains(TEST_GRANDCHILD_ID, TEST_COW_PAGE));
}

TEST(Stage10CopyOnWriteTest, ReportsInvalidAndNonCowFaults)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{128}, mm::PageNumber{48}};
    proc::Process parent{
        TEST_PARENT_ID,
        make_address_space(machine, TEST_PARENT_ROOT, TEST_PARENT_NEXT, TEST_PARENT_END)};
    proc::CopyOnWriteManager cow_manager;
    cpu::CpuState cpu_state;

    EXPECT_THAT(
        cow_manager.resolve_write_fault(allocator, machine.memory_bus(), parent, cpu_state),
        Eq(proc::CowFaultResult::INVALID_FAULT));

    arm_cow_fault(cpu_state);
    EXPECT_THAT(
        cow_manager.resolve_write_fault(allocator, machine.memory_bus(), parent, cpu_state),
        Eq(proc::CowFaultResult::NOT_COW));
    EXPECT_TRUE(cpu_state.has_pending_trap());
}
