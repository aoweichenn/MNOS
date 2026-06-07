#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/trap_frame.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/page_fault_handler.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/platform/machine.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{32});
constexpr mm::PhysicalAddress TEST_ROOT_TABLE{mm::AddressValue{0x1000}};
constexpr mm::PhysicalAddress TEST_NEXT_TABLE{mm::AddressValue{0x2000}};
constexpr mm::PhysicalAddress TEST_TABLE_ARENA_END{mm::AddressValue{0x6000}};
constexpr mm::VirtualAddress TEST_FAULT_PAGE{mm::AddressValue{0x400000}};
constexpr mm::AddressValue TEST_FAULT_OFFSET = mm::AddressValue{0x88};
constexpr cpu::Qword TEST_USER_NOT_PRESENT_ERROR = cpu_memory::PAGE_FAULT_ERROR_USER_BIT;
constexpr cpu::Qword TEST_KERNEL_NOT_PRESENT_ERROR = cpu::Qword{0};
constexpr cpu::Qword TEST_USER_EXECUTE_NOT_PRESENT_ERROR =
    cpu_memory::PAGE_FAULT_ERROR_USER_BIT |
    cpu_memory::PAGE_FAULT_ERROR_INSTRUCTION_FETCH_BIT;
constexpr cpu::Qword TEST_KERNEL_EXECUTE_NOT_PRESENT_ERROR = cpu_memory::PAGE_FAULT_ERROR_INSTRUCTION_FETCH_BIT;
constexpr cpu::Qword TEST_USER_WRITE_PROTECTION_ERROR =
    cpu_memory::PAGE_FAULT_ERROR_PRESENT_BIT |
    cpu_memory::PAGE_FAULT_ERROR_WRITE_BIT |
    cpu_memory::PAGE_FAULT_ERROR_USER_BIT;
constexpr cpu::Qword TEST_QWORD_VALUE = cpu::Qword{0xA5A5A5A5A5A5A5A5ULL};

[[nodiscard]] cpu_system::TrapFrame make_page_fault_frame(const cpu::Qword error_code)
{
    return cpu_system::TrapFrame{
        cpu_system::TrapKind::EXCEPTION,
        cpu_system::InterruptVector::page_fault(),
        cpu::InstructionPointer{0},
        cpu::InstructionPointer{0},
        cpu::Qword{0},
        cpu::Qword{0},
        cpu_system::PrivilegeLevel::RING3,
        error_code};
}

[[nodiscard]] cpu_system::TrapFrame make_page_fault_frame_without_error_code()
{
    return cpu_system::TrapFrame{
        cpu_system::TrapKind::EXCEPTION,
        cpu_system::InterruptVector::page_fault(),
        cpu::InstructionPointer{0},
        cpu::InstructionPointer{0},
        cpu::Qword{0},
        cpu::Qword{0},
        cpu_system::PrivilegeLevel::RING3};
}
}

TEST(PageFaultHandlerTest, MapsDemandPageAndClearsPendingTrap)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{32}, mm::PageNumber{6}};
    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_TABLE_ARENA_END};
    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.paging().set_page_fault_linear_address(mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET));
    state.set_pending_trap(make_page_fault_frame(TEST_USER_NOT_PRESENT_ERROR));
    mm::PageFaultHandler handler{allocator, address_space, machine.memory_bus()};

    EXPECT_THAT(handler.handle(state.pending_trap(), state), Eq(mm::PageFaultResult::HANDLED));
    EXPECT_FALSE(state.has_pending_trap());
    EXPECT_TRUE(state.paging().is_enabled());

    cpu_memory::MemoryManagementUnit mmu;
    mmu.write(
        machine.memory_bus(),
        state.paging(),
        cpu_system::PrivilegeLevel::RING3,
        mm::to_cpu_address(TEST_FAULT_PAGE + TEST_FAULT_OFFSET),
        cpu::DataSize::QWORD,
        TEST_QWORD_VALUE);
    EXPECT_THAT(
        machine.physical_memory().read_qword(cpu::Address64{0x6000} + TEST_FAULT_OFFSET),
        Eq(TEST_QWORD_VALUE));
    EXPECT_THAT(allocator.free_page_count(), Eq(mm::PageNumber{25}));
}

TEST(PageFaultHandlerTest, RejectsProtectionAndNonPageFaultFrames)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{32}, mm::PageNumber{6}};
    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_TABLE_ARENA_END};
    cpu::CpuState state;
    mm::PageFaultHandler handler{allocator, address_space, machine.memory_bus()};

    const cpu_system::TrapFrame protection_fault = make_page_fault_frame(TEST_USER_WRITE_PROTECTION_ERROR);
    EXPECT_THAT(handler.handle(protection_fault, state), Eq(mm::PageFaultResult::PROTECTION_FAULT));
    EXPECT_THAT(handler.handle(make_page_fault_frame_without_error_code(), state), Eq(mm::PageFaultResult::PROTECTION_FAULT));

    const cpu_system::TrapFrame breakpoint{
        cpu_system::TrapKind::SOFTWARE_INTERRUPT,
        cpu_system::InterruptVector::breakpoint(),
        cpu::InstructionPointer{0},
        cpu::InstructionPointer{0},
        cpu::Qword{0},
        cpu::Qword{0},
        cpu_system::PrivilegeLevel::RING3};
    EXPECT_THAT(handler.handle(breakpoint, state), Eq(mm::PageFaultResult::NOT_PAGE_FAULT));
}

TEST(PageFaultHandlerTest, FreesAllocatedPageWhenPageTableArenaIsExhausted)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{32}, mm::PageNumber{6}};
    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_ROOT_TABLE + mm::MM_PAGE_SIZE_BYTES};
    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.paging().set_page_fault_linear_address(mm::to_cpu_address(TEST_FAULT_PAGE));
    state.set_pending_trap(make_page_fault_frame(TEST_USER_NOT_PRESENT_ERROR));
    const mm::PageNumber free_pages_before = allocator.free_page_count();
    mm::PageFaultHandler handler{allocator, address_space, machine.memory_bus()};

    EXPECT_THAT(handler.handle(state.pending_trap(), state), Eq(mm::PageFaultResult::OUT_OF_MEMORY));
    EXPECT_THAT(allocator.free_page_count(), Eq(free_pages_before));
    EXPECT_TRUE(state.has_pending_trap());
}

TEST(PageFaultHandlerTest, ReportsOutOfMemoryWhenNoPhysicalPageIsAvailable)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{6}, mm::PageNumber{6}};
    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_TABLE_ARENA_END};
    cpu::CpuState state;
    state.paging().set_page_fault_linear_address(mm::to_cpu_address(TEST_FAULT_PAGE));
    state.set_pending_trap(make_page_fault_frame(TEST_KERNEL_NOT_PRESENT_ERROR));
    mm::PageFaultHandler handler{allocator, address_space, machine.memory_bus()};

    EXPECT_THAT(handler.handle(state.pending_trap(), state), Eq(mm::PageFaultResult::OUT_OF_MEMORY));
    EXPECT_TRUE(state.has_pending_trap());
}

TEST(PageFaultHandlerTest, MapsPermissionVariantsFromFaultErrorCode)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{32}, mm::PageNumber{6}};
    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_TABLE_ARENA_END};
    mm::PageFaultHandler handler{allocator, address_space, machine.memory_bus()};
    cpu_memory::MemoryManagementUnit mmu;

    cpu::CpuState user_execute_state;
    const mm::VirtualAddress user_execute_page = TEST_FAULT_PAGE + (mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{1});
    user_execute_state.paging().set_page_fault_linear_address(mm::to_cpu_address(user_execute_page));
    user_execute_state.set_pending_trap(make_page_fault_frame(TEST_USER_EXECUTE_NOT_PRESENT_ERROR));
    EXPECT_THAT(handler.handle(user_execute_state.pending_trap(), user_execute_state), Eq(mm::PageFaultResult::HANDLED));
    EXPECT_THAT(
        mmu.translate(
            machine.memory_bus(),
            user_execute_state.paging(),
            cpu_system::PrivilegeLevel::RING3,
            mm::to_cpu_address(user_execute_page),
            cpu_memory::MemoryAccessKind::EXECUTE),
        Eq(cpu::Address64{0x6000}));

    cpu::CpuState kernel_execute_state;
    const mm::VirtualAddress kernel_execute_page = TEST_FAULT_PAGE + (mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2});
    kernel_execute_state.paging().set_page_fault_linear_address(mm::to_cpu_address(kernel_execute_page));
    kernel_execute_state.set_pending_trap(make_page_fault_frame(TEST_KERNEL_EXECUTE_NOT_PRESENT_ERROR));
    EXPECT_THAT(handler.handle(kernel_execute_state.pending_trap(), kernel_execute_state), Eq(mm::PageFaultResult::HANDLED));
    EXPECT_THAT(
        mmu.translate(
            machine.memory_bus(),
            kernel_execute_state.paging(),
            cpu_system::PrivilegeLevel::RING0,
            mm::to_cpu_address(kernel_execute_page),
            cpu_memory::MemoryAccessKind::EXECUTE),
        Eq(cpu::Address64{0x7000}));

    cpu::CpuState kernel_data_state;
    const mm::VirtualAddress kernel_data_page = TEST_FAULT_PAGE + (mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{3});
    kernel_data_state.paging().set_page_fault_linear_address(mm::to_cpu_address(kernel_data_page));
    kernel_data_state.set_pending_trap(make_page_fault_frame(TEST_KERNEL_NOT_PRESENT_ERROR));
    EXPECT_THAT(handler.handle(kernel_data_state.pending_trap(), kernel_data_state), Eq(mm::PageFaultResult::HANDLED));
    EXPECT_THAT(
        mmu.translate(
            machine.memory_bus(),
            kernel_data_state.paging(),
            cpu_system::PrivilegeLevel::RING0,
            mm::to_cpu_address(kernel_data_page),
            cpu_memory::MemoryAccessKind::WRITE),
        Eq(cpu::Address64{0x8000}));
}
