#include <limits>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{32});
constexpr mm::PhysicalAddress TEST_ROOT_TABLE{mm::AddressValue{0x1000}};
constexpr mm::PhysicalAddress TEST_NEXT_TABLE{mm::AddressValue{0x2000}};
constexpr mm::PhysicalAddress TEST_TABLE_ARENA_END{mm::AddressValue{0x6000}};
constexpr mm::VirtualAddress TEST_USER_PAGE{mm::AddressValue{0x400000}};
constexpr mm::PhysicalAddress TEST_PHYSICAL_PAGE{mm::AddressValue{0x9000}};
constexpr cpu::Qword TEST_QWORD_VALUE = cpu::Qword{0x1122334455667788ULL};
}

TEST(AddressSpaceTest, BuildsMappingsAndActivatesCpuPaging)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_TABLE_ARENA_END};

    EXPECT_THAT(address_space.root_table_address(), Eq(TEST_ROOT_TABLE));
    EXPECT_THAT(address_space.next_free_table_address(), Eq(TEST_NEXT_TABLE));
    EXPECT_THAT(address_space.table_arena_end_address(), Eq(TEST_TABLE_ARENA_END));
    EXPECT_TRUE(address_space.has_available_table_pages());
    EXPECT_THAT(address_space.memory_bus().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    const mm::AddressSpace& const_address_space = address_space;
    EXPECT_THAT(const_address_space.memory_bus().size(), Eq(TEST_MEMORY_SIZE_BYTES));

    address_space.map_page(
        TEST_USER_PAGE,
        TEST_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_write_no_execute());
    EXPECT_THAT(address_space.next_free_table_address(), Eq(mm::PhysicalAddress{mm::AddressValue{0x5000}}));

    cpu::CpuState state;
    state.set_privilege_level(cpu::system::PrivilegeLevel::RING3);
    address_space.activate(state);
    EXPECT_TRUE(state.paging().is_enabled());
    EXPECT_THAT(state.paging().cr3(), Eq(mm::to_cpu_address(TEST_ROOT_TABLE)));

    cpu_memory::MemoryManagementUnit mmu;
    mmu.write(
        machine.memory_bus(),
        state.paging(),
        state.privilege_level(),
        mm::to_cpu_address(TEST_USER_PAGE),
        cpu::DataSize::QWORD,
        TEST_QWORD_VALUE);
    EXPECT_THAT(machine.physical_memory().read_qword(mm::to_cpu_address(TEST_PHYSICAL_PAGE)), Eq(TEST_QWORD_VALUE));
}

TEST(AddressSpaceTest, MapsRangesAndIdentityRanges)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_TABLE_ARENA_END};

    address_space.map_range(
        TEST_USER_PAGE,
        TEST_PHYSICAL_PAGE,
        mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2},
        cpu_memory::PagePermissions::user_read_write_execute());
    address_space.identity_map_range(
        mm::PhysicalAddress{mm::AddressValue{0xB000}},
        mm::MM_PAGE_SIZE_BYTES,
        cpu_memory::PagePermissions::kernel_read_write_execute());

    cpu::CpuState state;
    address_space.activate(state);
    cpu_memory::MemoryManagementUnit mmu;
    EXPECT_THAT(
        mmu.translate(
            machine.memory_bus(),
            state.paging(),
            cpu::system::PrivilegeLevel::RING3,
            mm::to_cpu_address(TEST_USER_PAGE + mm::MM_PAGE_SIZE_BYTES),
            cpu_memory::MemoryAccessKind::READ),
        Eq(mm::to_cpu_address(TEST_PHYSICAL_PAGE + mm::MM_PAGE_SIZE_BYTES)));
    EXPECT_THAT(
        mmu.translate(
            machine.memory_bus(),
            state.paging(),
            cpu::system::PrivilegeLevel::RING0,
            cpu::Address64{0xB000},
            cpu_memory::MemoryAccessKind::READ),
        Eq(cpu::Address64{0xB000}));
}

TEST(AddressSpaceTest, RejectsInvalidMappingsAndExhaustedTableArena)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    EXPECT_THROW(
        static_cast<void>(mm::AddressSpace{
            machine.memory_bus(),
            mm::PhysicalAddress{mm::AddressValue{1}},
            TEST_NEXT_TABLE,
            TEST_TABLE_ARENA_END}),
        std::invalid_argument);

    mm::AddressSpace address_space{
        machine.memory_bus(),
        TEST_ROOT_TABLE,
        TEST_NEXT_TABLE,
        TEST_ROOT_TABLE + mm::MM_PAGE_SIZE_BYTES};

    EXPECT_THROW(
        address_space.map_page(
            TEST_USER_PAGE,
            TEST_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute()),
        std::out_of_range);
    EXPECT_THROW(
        address_space.map_page(
            TEST_USER_PAGE,
            TEST_PHYSICAL_PAGE + mm::AddressValue{1},
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::invalid_argument);
    EXPECT_THROW(
        address_space.map_range(
            TEST_USER_PAGE + mm::AddressValue{1},
            TEST_PHYSICAL_PAGE,
            mm::MM_PAGE_SIZE_BYTES,
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::invalid_argument);
    EXPECT_THROW(
        address_space.map_range(
            TEST_USER_PAGE,
            TEST_PHYSICAL_PAGE,
            mm::AddressValue{0},
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::invalid_argument);
    EXPECT_THROW(
        address_space.map_range(
            TEST_USER_PAGE,
            TEST_PHYSICAL_PAGE,
            mm::MM_PAGE_SIZE_BYTES + mm::AddressValue{1},
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::invalid_argument);
    EXPECT_THROW(
        address_space.map_range(
            mm::VirtualAddress{std::numeric_limits<mm::AddressValue>::max() & mm::MM_PAGE_FRAME_MASK},
            TEST_PHYSICAL_PAGE,
            mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2},
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::overflow_error);
    EXPECT_THROW(
        address_space.map_range(
            TEST_USER_PAGE,
            mm::PhysicalAddress{std::numeric_limits<mm::AddressValue>::max() & mm::MM_PAGE_FRAME_MASK},
            mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2},
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::overflow_error);
}

TEST(AddressSpaceTest, RejectsInvalidTableArenaConstruction)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);

    EXPECT_THROW(
        static_cast<void>(mm::AddressSpace{
            machine.memory_bus(),
            TEST_ROOT_TABLE,
            TEST_NEXT_TABLE,
            TEST_ROOT_TABLE}),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(mm::AddressSpace{
            machine.memory_bus(),
            TEST_ROOT_TABLE,
            TEST_TABLE_ARENA_END + mm::MM_PAGE_SIZE_BYTES,
            TEST_TABLE_ARENA_END}),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(mm::AddressSpace{
            machine.memory_bus(),
            TEST_NEXT_TABLE,
            TEST_ROOT_TABLE,
            TEST_TABLE_ARENA_END}),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(mm::AddressSpace{
            machine.memory_bus(),
            mm::PhysicalAddress{static_cast<mm::AddressValue>(TEST_MEMORY_SIZE_BYTES)},
            mm::PhysicalAddress{static_cast<mm::AddressValue>(TEST_MEMORY_SIZE_BYTES) + mm::MM_PAGE_SIZE_BYTES},
            mm::PhysicalAddress{static_cast<mm::AddressValue>(TEST_MEMORY_SIZE_BYTES) + (mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2})}}),
        std::out_of_range);
}
