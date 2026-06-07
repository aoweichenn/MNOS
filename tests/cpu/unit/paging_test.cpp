#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/page_table_builder.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/memory/tlb.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_ACCESS_KIND =
    static_cast<cpu_memory::MemoryAccessKind>(cpu_memory::MEMORY_ACCESS_KIND_COUNT);
constexpr auto TEST_INVALID_PAGE_FAULT_REASON =
    static_cast<cpu_memory::PageFaultReason>(cpu_memory::PAGE_FAULT_REASON_COUNT);
constexpr auto TEST_INVALID_PAGE_TABLE_LEVEL =
    static_cast<cpu_memory::PageTableLevel>(cpu_memory::PAGE_TABLE_LEVEL_ENUM_COUNT);

constexpr std::size_t TEST_PAGING_MEMORY_SIZE_BYTES = 128 * 1024;
constexpr cpu::Address64 TEST_PAGING_ROOT_TABLE = cpu::Address64{0x1000};
constexpr cpu::Address64 TEST_PAGING_NEXT_TABLE = cpu::Address64{0x2000};
constexpr cpu::Address64 TEST_PAGING_PDPT_TABLE = cpu::Address64{0x2000};
constexpr cpu::Address64 TEST_PAGING_PT_TABLE = cpu::Address64{0x4000};
constexpr cpu::Address64 TEST_PAGING_LINEAR_PAGE = cpu::Address64{0x5000};
constexpr cpu::Address64 TEST_PAGING_SECOND_LINEAR_PAGE = cpu::Address64{0x6000};
constexpr cpu::Address64 TEST_PAGING_PHYSICAL_PAGE = cpu::Address64{0x9000};
constexpr cpu::Address64 TEST_PAGING_SECOND_PHYSICAL_PAGE = cpu::Address64{0xB000};
constexpr cpu::Address64 TEST_PAGING_LINEAR_OFFSET = cpu::Address64{0x28};
constexpr cpu::Address64 TEST_PAGING_LINEAR_ADDRESS = TEST_PAGING_LINEAR_PAGE + TEST_PAGING_LINEAR_OFFSET;
constexpr cpu::Address64 TEST_PAGING_PHYSICAL_ADDRESS = TEST_PAGING_PHYSICAL_PAGE + TEST_PAGING_LINEAR_OFFSET;
constexpr cpu::Address64 TEST_PAGING_CROSS_PAGE_LINEAR_ADDRESS =
    TEST_PAGING_SECOND_LINEAR_PAGE - cpu::Address64{4};
constexpr cpu::Address64 TEST_PAGING_LARGE_PAGE_2M_LINEAR = cpu::Address64{0x200000};
constexpr cpu::Address64 TEST_PAGING_LARGE_PAGE_1G_LINEAR = cpu::Address64{0x40000000};
constexpr cpu::Address64 TEST_PAGING_LARGE_PAGE_OFFSET = cpu::Address64{0x1234};
constexpr cpu::Qword TEST_PAGING_QWORD_VALUE = cpu::Qword{0x0102030405060708ULL};
constexpr cpu::Qword TEST_PAGING_SECOND_QWORD_VALUE = cpu::Qword{0x8877665544332211ULL};
constexpr cpu::Qword TEST_PAGING_RESERVED_ENTRY =
    cpu_memory::PAGE_TABLE_ENTRY_PRESENT_BIT | cpu::Qword{0x0010'0000'0000'0000ULL};
constexpr cpu::Qword TEST_PAGING_EXPECTED_USER_WRITE_FAULT =
    cpu_memory::PAGE_FAULT_ERROR_PRESENT_BIT |
    cpu_memory::PAGE_FAULT_ERROR_WRITE_BIT |
    cpu_memory::PAGE_FAULT_ERROR_USER_BIT;
constexpr cpu::Qword TEST_PAGING_EXPECTED_USER_EXECUTE_FAULT =
    cpu_memory::PAGE_FAULT_ERROR_PRESENT_BIT |
    cpu_memory::PAGE_FAULT_ERROR_USER_BIT |
    cpu_memory::PAGE_FAULT_ERROR_INSTRUCTION_FETCH_BIT;
constexpr cpu::Qword TEST_PAGING_EXPECTED_RESERVED_FAULT =
    cpu_memory::PAGE_FAULT_ERROR_PRESENT_BIT |
    cpu_memory::PAGE_FAULT_ERROR_RESERVED_BIT;

[[nodiscard]] cpu::Address64 page_table_entry_address(
    const cpu::Address64 table_address,
    const std::size_t entry_index) noexcept
{
    return table_address +
        static_cast<cpu::Address64>(entry_index * cpu_memory::PAGE_TABLE_ENTRY_BYTES);
}

[[nodiscard]] cpu::Address64 leaf_entry_address(const cpu::Address64 linear_address) noexcept
{
    return page_table_entry_address(
        TEST_PAGING_PT_TABLE,
        cpu_memory::page_table_index(linear_address, cpu_memory::PageTableLevel::PT));
}
}

TEST(PagingTest, ModelsPageEntriesFaultCodesAndControlState)
{
    EXPECT_TRUE(cpu_memory::is_memory_access_kind_valid(cpu_memory::MemoryAccessKind::READ));
    EXPECT_FALSE(cpu_memory::is_memory_access_kind_valid(TEST_INVALID_ACCESS_KIND));
    EXPECT_THAT(cpu_memory::memory_access_kind_to_index(cpu_memory::MemoryAccessKind::WRITE), Eq(std::size_t{1}));
    EXPECT_THAT(
        cpu_memory::memory_access_kind_to_name(cpu_memory::MemoryAccessKind::EXECUTE),
        Eq(std::string_view{"EXECUTE"}));

    EXPECT_TRUE(cpu_memory::is_page_fault_reason_valid(cpu_memory::PageFaultReason::NOT_PRESENT));
    EXPECT_FALSE(cpu_memory::is_page_fault_reason_valid(TEST_INVALID_PAGE_FAULT_REASON));
    EXPECT_THAT(
        cpu_memory::page_fault_reason_to_name(cpu_memory::PageFaultReason::RESERVED_BIT),
        Eq(std::string_view{"RESERVED_BIT"}));
    EXPECT_THAT(
        cpu_memory::page_fault_reason_to_index(cpu_memory::PageFaultReason::EXECUTE_DISABLE),
        Eq(std::size_t{3}));

    EXPECT_TRUE(cpu_memory::is_page_table_level_valid(cpu_memory::PageTableLevel::PML4));
    EXPECT_FALSE(cpu_memory::is_page_table_level_valid(TEST_INVALID_PAGE_TABLE_LEVEL));
    EXPECT_THAT(
        cpu_memory::page_table_level_to_index(cpu_memory::PageTableLevel::PD),
        Eq(std::size_t{1}));
    EXPECT_THAT(
        cpu_memory::page_table_level_shift(cpu_memory::PageTableLevel::PDPT),
        Eq(cpu_memory::PAGE_TABLE_INDEX_SHIFT_PDPT));
    EXPECT_THAT(
        cpu_memory::page_table_level_to_name(cpu_memory::PageTableLevel::PT),
        Eq(std::string_view{"PT"}));
    EXPECT_THROW(
        static_cast<void>(cpu_memory::page_size_for_leaf_level(cpu_memory::PageTableLevel::PML4)),
        std::logic_error);
    EXPECT_THROW(
        static_cast<void>(cpu_memory::page_table_level_shift(TEST_INVALID_PAGE_TABLE_LEVEL)),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(cpu_memory::page_size_for_leaf_level(TEST_INVALID_PAGE_TABLE_LEVEL)),
        std::out_of_range);
    EXPECT_TRUE(cpu_memory::can_page_table_level_be_leaf(cpu_memory::PageTableLevel::PD));
    EXPECT_FALSE(cpu_memory::can_page_table_level_be_leaf(cpu_memory::PageTableLevel::PML4));

    EXPECT_TRUE(cpu_memory::is_canonical_linear_address(cpu::Address64{0x00007FFF'FFFFF000ULL}));
    EXPECT_FALSE(cpu_memory::is_canonical_linear_address(cpu::Address64{0x00008000'00000000ULL}));
    EXPECT_THAT(
        cpu_memory::page_table_index(TEST_PAGING_LINEAR_ADDRESS, cpu_memory::PageTableLevel::PT),
        Eq(std::size_t{5}));
    EXPECT_THAT(
        cpu_memory::page_base(TEST_PAGING_LINEAR_ADDRESS, cpu_memory::PAGE_SIZE_4K_BYTES),
        Eq(TEST_PAGING_LINEAR_PAGE));

    const cpu_memory::PagePermissions user_no_execute =
        cpu_memory::PagePermissions::user_read_write_no_execute();
    EXPECT_FALSE(cpu_memory::PagePermissions::kernel_read_only_execute().writable());
    EXPECT_FALSE(cpu_memory::PagePermissions::kernel_read_write_no_execute().executable());
    const cpu_memory::PageTableEntry user_entry =
        cpu_memory::PageTableEntry::page_4k(TEST_PAGING_PHYSICAL_PAGE, user_no_execute);
    EXPECT_TRUE(user_entry.is_present());
    EXPECT_TRUE(user_entry.is_writable());
    EXPECT_TRUE(user_entry.is_user_accessible());
    EXPECT_TRUE(user_entry.is_no_execute());
    EXPECT_FALSE(user_entry.is_accessed());
    EXPECT_THAT(user_entry.frame_address(cpu_memory::PAGE_SIZE_4K_BYTES), Eq(TEST_PAGING_PHYSICAL_PAGE));
    EXPECT_TRUE(user_entry.with_dirty().is_dirty());
    EXPECT_FALSE(cpu_memory::PageTableEntry::not_present().is_present());

    const cpu_memory::PageTableEntry cache_control_entry{
        cpu_memory::PAGE_TABLE_ENTRY_PRESENT_BIT |
        cpu_memory::PAGE_TABLE_ENTRY_WRITE_THROUGH_BIT |
        cpu_memory::PAGE_TABLE_ENTRY_CACHE_DISABLE_BIT |
        cpu_memory::PAGE_TABLE_ENTRY_GLOBAL_BIT};
    EXPECT_TRUE(cache_control_entry.is_write_through());
    EXPECT_TRUE(cache_control_entry.is_cache_disabled());
    EXPECT_TRUE(cache_control_entry.is_global());
    EXPECT_THROW(
        static_cast<void>(cpu_memory::PageTableEntry::page_4k(
            TEST_PAGING_PHYSICAL_PAGE + cpu::Address64{1},
            user_no_execute)),
        std::out_of_range);

    cpu_memory::PagingState paging_state;
    EXPECT_FALSE(paging_state.is_enabled());
    EXPECT_TRUE(paging_state.write_protect_enabled());
    EXPECT_TRUE(paging_state.execute_disable_enabled());
    EXPECT_THROW(paging_state.load_cr3(TEST_PAGING_ROOT_TABLE + cpu::Address64{1}), std::out_of_range);
    paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    const cpu::Qword generation_after_cr3 = paging_state.generation();
    paging_state.enable();
    EXPECT_TRUE(paging_state.is_enabled());
    EXPECT_THAT(paging_state.cr3(), Eq(TEST_PAGING_ROOT_TABLE));
    EXPECT_THAT(paging_state.generation(), Eq(generation_after_cr3 + cpu::Qword{1}));
    paging_state.set_page_fault_linear_address(TEST_PAGING_LINEAR_ADDRESS);
    EXPECT_THAT(paging_state.page_fault_linear_address(), Eq(TEST_PAGING_LINEAR_ADDRESS));
    paging_state.set_write_protect_enabled(false);
    EXPECT_FALSE(paging_state.write_protect_enabled());
    paging_state.set_execute_disable_enabled(false);
    EXPECT_FALSE(paging_state.execute_disable_enabled());
    paging_state.disable();
    EXPECT_FALSE(paging_state.is_enabled());

    EXPECT_THAT(
        cpu_memory::make_page_fault_error_code(
            cpu_memory::MemoryAccessKind::WRITE,
            cpu_system::PrivilegeLevel::RING3,
            cpu_memory::PageFaultReason::WRITE_PROTECTION),
        Eq(TEST_PAGING_EXPECTED_USER_WRITE_FAULT));
    EXPECT_THROW(
        static_cast<void>(cpu_memory::PageFault{
            TEST_PAGING_LINEAR_ADDRESS,
            TEST_INVALID_ACCESS_KIND,
            cpu_memory::PageFaultReason::NOT_PRESENT,
            cpu::Qword{0}}),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(cpu_memory::PageFault{
            TEST_PAGING_LINEAR_ADDRESS,
            cpu_memory::MemoryAccessKind::READ,
            TEST_INVALID_PAGE_FAULT_REASON,
            cpu::Qword{0}}),
        std::out_of_range);

    const cpu_memory::PageTranslation translation{
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PAGE_SIZE_4K_BYTES,
        cpu_memory::PagePermissions::user_read_write_execute(),
        leaf_entry_address(TEST_PAGING_LINEAR_PAGE),
        false};
    EXPECT_THAT(translation.physical_frame_base(), Eq(TEST_PAGING_PHYSICAL_PAGE));
    EXPECT_THAT(translation.page_size_bytes(), Eq(cpu_memory::PAGE_SIZE_4K_BYTES));
    const cpu_memory::PageFault page_fault{
        TEST_PAGING_LINEAR_ADDRESS,
        cpu_memory::MemoryAccessKind::WRITE,
        cpu_memory::PageFaultReason::WRITE_PROTECTION,
        TEST_PAGING_EXPECTED_USER_WRITE_FAULT};
    EXPECT_THAT(page_fault.access_kind(), Eq(cpu_memory::MemoryAccessKind::WRITE));
}

TEST(TranslationLookasideBufferTest, CachesTranslationsAndInvalidatesByGenerationOrPage)
{
    cpu_memory::TranslationLookasideBuffer tlb;
    EXPECT_TRUE(tlb.empty());
    EXPECT_THAT(tlb.capacity(), Eq(cpu_memory::TRANSLATION_LOOKASIDE_BUFFER_ENTRY_COUNT));

    const cpu_memory::PageTranslation translation{
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PAGE_SIZE_4K_BYTES,
        cpu_memory::PagePermissions::user_read_write_execute(),
        leaf_entry_address(TEST_PAGING_LINEAR_PAGE),
        false};
    tlb.insert(translation, cpu::Qword{1});
    EXPECT_FALSE(tlb.empty());
    EXPECT_THAT(tlb.size(), Eq(std::size_t{1}));
    ASSERT_NE(tlb.lookup(TEST_PAGING_LINEAR_ADDRESS, cpu::Qword{1}), nullptr);
    EXPECT_THAT(
        tlb.lookup(TEST_PAGING_LINEAR_ADDRESS, cpu::Qword{1})->translate(TEST_PAGING_LINEAR_ADDRESS),
        Eq(TEST_PAGING_PHYSICAL_ADDRESS));
    const cpu_memory::TranslationLookasideBuffer& const_tlb = tlb;
    ASSERT_NE(const_tlb.lookup(TEST_PAGING_LINEAR_ADDRESS, cpu::Qword{1}), nullptr);
    EXPECT_THAT(
        const_tlb.lookup(TEST_PAGING_LINEAR_ADDRESS, cpu::Qword{1})->translate(TEST_PAGING_LINEAR_ADDRESS),
        Eq(TEST_PAGING_PHYSICAL_ADDRESS));
    EXPECT_EQ(tlb.lookup(TEST_PAGING_LINEAR_ADDRESS, cpu::Qword{2}), nullptr);

    const cpu::Address64 colliding_linear_page =
        TEST_PAGING_LINEAR_PAGE +
        static_cast<cpu::Address64>(
            cpu_memory::TRANSLATION_LOOKASIDE_BUFFER_ENTRY_COUNT * cpu_memory::PAGE_SIZE_4K_BYTES);
    tlb.insert(
        cpu_memory::PageTranslation{
            colliding_linear_page,
            TEST_PAGING_SECOND_PHYSICAL_PAGE,
            cpu_memory::PAGE_SIZE_4K_BYTES,
            cpu_memory::PagePermissions::user_read_write_execute(),
            leaf_entry_address(TEST_PAGING_LINEAR_PAGE),
            false},
        cpu::Qword{1});
    EXPECT_THAT(tlb.size(), Eq(std::size_t{1}));
    EXPECT_EQ(tlb.lookup(TEST_PAGING_LINEAR_ADDRESS, cpu::Qword{1}), nullptr);

    tlb.invalidate_page(colliding_linear_page);
    EXPECT_TRUE(tlb.empty());
    tlb.insert(translation, cpu::Qword{1});
    tlb.clear();
    EXPECT_TRUE(tlb.empty());
}

TEST(PageTableBuilderTest, BuildsTablesAndRejectsInvalidMappings)
{
    cpu::PhysicalMemory memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder builder{memory_bus, TEST_PAGING_ROOT_TABLE, TEST_PAGING_NEXT_TABLE};

    builder.clear_root_table();
    builder.map_4k(
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute());
    EXPECT_THAT(builder.root_table_address(), Eq(TEST_PAGING_ROOT_TABLE));
    EXPECT_THAT(builder.next_free_table_address(), Eq(TEST_PAGING_PT_TABLE + cpu_memory::PAGE_SIZE_4K_BYTES));

    const cpu_memory::PageTableEntry pml4_entry{
        memory_bus.read(
            page_table_entry_address(
                TEST_PAGING_ROOT_TABLE,
                cpu_memory::page_table_index(TEST_PAGING_LINEAR_PAGE, cpu_memory::PageTableLevel::PML4)),
            cpu::DataSize::QWORD)};
    EXPECT_TRUE(pml4_entry.is_present());
    EXPECT_THAT(pml4_entry.table_address(), Eq(TEST_PAGING_PDPT_TABLE));

    EXPECT_THROW(
        builder.map_4k(
            TEST_PAGING_LINEAR_PAGE + cpu::Address64{1},
            TEST_PAGING_PHYSICAL_PAGE,
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::out_of_range);

    cpu::PhysicalMemory small_memory(cpu_memory::PAGE_SIZE_4K_BYTES * std::size_t{2});
    cpu::MemoryBus small_bus{small_memory};
    cpu_memory::PageTableBuilder small_builder{
        small_bus,
        cpu::Address64{0},
        cpu_memory::PAGE_SIZE_4K_BYTES};
    small_builder.clear_root_table();
    EXPECT_THROW(
        small_builder.map_4k(
            TEST_PAGING_LINEAR_PAGE,
            TEST_PAGING_PHYSICAL_PAGE,
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::out_of_range);

    cpu::PhysicalMemory conflict_memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus conflict_bus{conflict_memory};
    cpu_memory::PageTableBuilder conflict_builder{
        conflict_bus,
        TEST_PAGING_ROOT_TABLE,
        TEST_PAGING_NEXT_TABLE};
    conflict_builder.clear_root_table();
    conflict_builder.map_1g(
        cpu::Address64{0},
        cpu::Address64{0},
        cpu_memory::PagePermissions::user_read_write_execute());
    EXPECT_THROW(
        conflict_builder.map_4k(
            cpu::Address64{0},
            TEST_PAGING_PHYSICAL_PAGE,
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::logic_error);

    cpu::PhysicalMemory large_page_conflict_memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus large_page_conflict_bus{large_page_conflict_memory};
    cpu_memory::PageTableBuilder large_page_conflict_builder{
        large_page_conflict_bus,
        TEST_PAGING_ROOT_TABLE,
        TEST_PAGING_NEXT_TABLE};
    large_page_conflict_builder.clear_root_table();
    large_page_conflict_builder.map_4k(
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute());
    EXPECT_THROW(
        large_page_conflict_builder.map_2m(
            cpu::Address64{0},
            cpu::Address64{0},
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::logic_error);

    cpu::PhysicalMemory huge_page_conflict_memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus huge_page_conflict_bus{huge_page_conflict_memory};
    cpu_memory::PageTableBuilder huge_page_conflict_builder{
        huge_page_conflict_bus,
        TEST_PAGING_ROOT_TABLE,
        TEST_PAGING_NEXT_TABLE};
    huge_page_conflict_builder.clear_root_table();
    huge_page_conflict_builder.map_2m(
        cpu::Address64{0},
        cpu::Address64{0},
        cpu_memory::PagePermissions::user_read_write_execute());
    EXPECT_THROW(
        huge_page_conflict_builder.map_1g(
            cpu::Address64{0},
            cpu::Address64{0},
            cpu_memory::PagePermissions::user_read_write_execute()),
        std::logic_error);
}

TEST(MemoryManagementUnitTest, TranslatesMappedPagesAndUpdatesAccessedDirtyBits)
{
    cpu::PhysicalMemory memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder builder{memory_bus, TEST_PAGING_ROOT_TABLE, TEST_PAGING_NEXT_TABLE};
    builder.clear_root_table();
    builder.map_4k(
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute());
    memory.write_qword(TEST_PAGING_PHYSICAL_ADDRESS, TEST_PAGING_QWORD_VALUE);

    cpu_memory::PagingState paging_state;
    paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    paging_state.enable();
    cpu_memory::MemoryManagementUnit mmu;

    EXPECT_THAT(
        mmu.read(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            TEST_PAGING_LINEAR_ADDRESS,
            cpu::DataSize::QWORD),
        Eq(TEST_PAGING_QWORD_VALUE));
    EXPECT_THAT(mmu.tlb().size(), Eq(std::size_t{1}));

    const cpu_memory::PageTableEntry accessed_leaf{
        memory_bus.read(leaf_entry_address(TEST_PAGING_LINEAR_PAGE), cpu::DataSize::QWORD)};
    EXPECT_TRUE(accessed_leaf.is_accessed());
    EXPECT_FALSE(accessed_leaf.is_dirty());

    mmu.write(
        memory_bus,
        paging_state,
        cpu_system::PrivilegeLevel::RING3,
        TEST_PAGING_LINEAR_ADDRESS,
        cpu::DataSize::QWORD,
        TEST_PAGING_SECOND_QWORD_VALUE);
    EXPECT_THAT(memory.read_qword(TEST_PAGING_PHYSICAL_ADDRESS), Eq(TEST_PAGING_SECOND_QWORD_VALUE));

    const cpu_memory::PageTableEntry dirty_leaf{
        memory_bus.read(leaf_entry_address(TEST_PAGING_LINEAR_PAGE), cpu::DataSize::QWORD)};
    EXPECT_TRUE(dirty_leaf.is_dirty());
}

TEST(MemoryManagementUnitTest, SupportsCrossPageVirtualAccessAndLargePageTranslations)
{
    cpu::PhysicalMemory memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder builder{memory_bus, TEST_PAGING_ROOT_TABLE, TEST_PAGING_NEXT_TABLE};
    builder.clear_root_table();
    builder.map_4k(
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute());
    builder.map_4k(
        TEST_PAGING_SECOND_LINEAR_PAGE,
        TEST_PAGING_SECOND_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute());
    builder.map_2m(
        TEST_PAGING_LARGE_PAGE_2M_LINEAR,
        cpu::Address64{0},
        cpu_memory::PagePermissions::kernel_read_write_execute());
    builder.map_1g(
        TEST_PAGING_LARGE_PAGE_1G_LINEAR,
        cpu::Address64{0},
        cpu_memory::PagePermissions::kernel_read_write_execute());

    cpu_memory::PagingState paging_state;
    paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    paging_state.enable();
    cpu_memory::MemoryManagementUnit mmu;

    mmu.write(
        memory_bus,
        paging_state,
        cpu_system::PrivilegeLevel::RING3,
        TEST_PAGING_CROSS_PAGE_LINEAR_ADDRESS,
        cpu::DataSize::QWORD,
        TEST_PAGING_QWORD_VALUE);
    EXPECT_THAT(
        mmu.read(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            TEST_PAGING_CROSS_PAGE_LINEAR_ADDRESS,
            cpu::DataSize::QWORD),
        Eq(TEST_PAGING_QWORD_VALUE));
    EXPECT_THAT(memory.read_byte(TEST_PAGING_PHYSICAL_PAGE + cpu::Address64{0xFFC}), Eq(cpu::Byte{0x08}));
    EXPECT_THAT(memory.read_byte(TEST_PAGING_SECOND_PHYSICAL_PAGE), Eq(cpu::Byte{0x04}));

    EXPECT_THAT(
        mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING0,
            TEST_PAGING_LARGE_PAGE_2M_LINEAR + TEST_PAGING_LARGE_PAGE_OFFSET,
            cpu_memory::MemoryAccessKind::READ),
        Eq(TEST_PAGING_LARGE_PAGE_OFFSET));
    EXPECT_THAT(
        mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING0,
            TEST_PAGING_LARGE_PAGE_1G_LINEAR + TEST_PAGING_LARGE_PAGE_OFFSET,
            cpu_memory::MemoryAccessKind::READ),
        Eq(TEST_PAGING_LARGE_PAGE_OFFSET));
}

TEST(MemoryManagementUnitTest, RaisesPageFaultsForMissingProtectedExecuteDisabledAndReservedPages)
{
    cpu::PhysicalMemory memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    cpu_memory::PagingState empty_paging_state;
    empty_paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    empty_paging_state.enable();
    cpu_memory::MemoryManagementUnit mmu;
    try
    {
        static_cast<void>(mmu.translate(
            memory_bus,
            empty_paging_state,
            cpu_system::PrivilegeLevel::RING0,
            TEST_PAGING_LINEAR_ADDRESS,
            cpu_memory::MemoryAccessKind::READ));
        FAIL() << "expected a not-present page fault";
    }
    catch (const cpu_memory::PageFault& fault)
    {
        EXPECT_THAT(fault.reason(), Eq(cpu_memory::PageFaultReason::NOT_PRESENT));
        EXPECT_THAT(fault.error_code(), Eq(cpu::Qword{0}));
    }

    cpu_memory::PageTableBuilder builder{memory_bus, TEST_PAGING_ROOT_TABLE, TEST_PAGING_NEXT_TABLE};
    builder.clear_root_table();
    builder.map_4k(
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::kernel_read_write_execute());
    builder.map_4k(
        TEST_PAGING_SECOND_LINEAR_PAGE,
        TEST_PAGING_SECOND_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_only_execute());
    cpu_memory::PagingState paging_state;
    paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    paging_state.enable();
    mmu.tlb().clear();

    EXPECT_THROW(
        static_cast<void>(mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            TEST_PAGING_LINEAR_ADDRESS,
            cpu_memory::MemoryAccessKind::READ)),
        cpu_memory::PageFault);

    try
    {
        static_cast<void>(mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            TEST_PAGING_SECOND_LINEAR_PAGE,
            cpu_memory::MemoryAccessKind::WRITE));
        FAIL() << "expected a user write-protection page fault";
    }
    catch (const cpu_memory::PageFault& fault)
    {
        EXPECT_THAT(fault.reason(), Eq(cpu_memory::PageFaultReason::WRITE_PROTECTION));
        EXPECT_THAT(fault.error_code(), Eq(TEST_PAGING_EXPECTED_USER_WRITE_FAULT));
    }

    paging_state.set_write_protect_enabled(false);
    mmu.tlb().clear();
    EXPECT_THAT(
        mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING0,
            TEST_PAGING_SECOND_LINEAR_PAGE,
            cpu_memory::MemoryAccessKind::WRITE),
        Eq(TEST_PAGING_SECOND_PHYSICAL_PAGE));

    builder.map_4k(
        cpu::Address64{0x7000},
        cpu::Address64{0xC000},
        cpu_memory::PagePermissions::user_read_write_no_execute());
    paging_state.set_write_protect_enabled(true);
    mmu.tlb().clear();
    try
    {
        static_cast<void>(mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            cpu::Address64{0x7000},
            cpu_memory::MemoryAccessKind::EXECUTE));
        FAIL() << "expected an execute-disable page fault";
    }
    catch (const cpu_memory::PageFault& fault)
    {
        EXPECT_THAT(fault.reason(), Eq(cpu_memory::PageFaultReason::EXECUTE_DISABLE));
        EXPECT_THAT(fault.error_code(), Eq(TEST_PAGING_EXPECTED_USER_EXECUTE_FAULT));
    }

    cpu::PhysicalMemory reserved_memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus reserved_bus{reserved_memory};
    reserved_bus.write(
        page_table_entry_address(
            TEST_PAGING_ROOT_TABLE,
            cpu_memory::page_table_index(TEST_PAGING_LINEAR_ADDRESS, cpu_memory::PageTableLevel::PML4)),
        cpu::DataSize::QWORD,
        TEST_PAGING_RESERVED_ENTRY);
    cpu_memory::PagingState reserved_paging_state;
    reserved_paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    reserved_paging_state.enable();
    cpu_memory::MemoryManagementUnit reserved_mmu;
    try
    {
        static_cast<void>(reserved_mmu.translate(
            reserved_bus,
            reserved_paging_state,
            cpu_system::PrivilegeLevel::RING0,
            TEST_PAGING_LINEAR_ADDRESS,
            cpu_memory::MemoryAccessKind::READ));
        FAIL() << "expected a reserved-bit page fault";
    }
    catch (const cpu_memory::PageFault& fault)
    {
        EXPECT_THAT(fault.reason(), Eq(cpu_memory::PageFaultReason::RESERVED_BIT));
        EXPECT_THAT(fault.error_code(), Eq(TEST_PAGING_EXPECTED_RESERVED_FAULT));
    }
}

TEST(MemoryManagementUnitTest, RechecksPermissionsOnCachedTranslations)
{
    cpu::PhysicalMemory memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder builder{memory_bus, TEST_PAGING_ROOT_TABLE, TEST_PAGING_NEXT_TABLE};
    builder.clear_root_table();
    builder.map_4k(
        TEST_PAGING_LINEAR_PAGE,
        TEST_PAGING_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::kernel_read_write_execute());
    builder.map_4k(
        TEST_PAGING_SECOND_LINEAR_PAGE,
        TEST_PAGING_SECOND_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::user_read_only_execute());
    builder.map_4k(
        cpu::Address64{0x7000},
        cpu::Address64{0xC000},
        cpu_memory::PagePermissions::user_read_write_no_execute());

    cpu_memory::PagingState paging_state;
    paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    paging_state.enable();
    cpu_memory::MemoryManagementUnit mmu;
    const cpu_memory::MemoryManagementUnit& const_mmu = mmu;
    EXPECT_TRUE(const_mmu.tlb().empty());

    static_cast<void>(mmu.translate(
        memory_bus,
        paging_state,
        cpu_system::PrivilegeLevel::RING0,
        TEST_PAGING_LINEAR_ADDRESS,
        cpu_memory::MemoryAccessKind::READ));
    try
    {
        static_cast<void>(mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            TEST_PAGING_LINEAR_ADDRESS,
            cpu_memory::MemoryAccessKind::READ));
        FAIL() << "expected cached user/supervisor page fault";
    }
    catch (const cpu_memory::PageFault& fault)
    {
        EXPECT_THAT(fault.reason(), Eq(cpu_memory::PageFaultReason::USER_SUPERVISOR));
    }

    mmu.tlb().clear();
    static_cast<void>(mmu.translate(
        memory_bus,
        paging_state,
        cpu_system::PrivilegeLevel::RING3,
        TEST_PAGING_SECOND_LINEAR_PAGE,
        cpu_memory::MemoryAccessKind::READ));
    try
    {
        static_cast<void>(mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            TEST_PAGING_SECOND_LINEAR_PAGE,
            cpu_memory::MemoryAccessKind::WRITE));
        FAIL() << "expected cached write-protection page fault";
    }
    catch (const cpu_memory::PageFault& fault)
    {
        EXPECT_THAT(fault.reason(), Eq(cpu_memory::PageFaultReason::WRITE_PROTECTION));
    }

    paging_state.set_write_protect_enabled(false);
    mmu.tlb().clear();
    static_cast<void>(mmu.translate(
        memory_bus,
        paging_state,
        cpu_system::PrivilegeLevel::RING0,
        TEST_PAGING_SECOND_LINEAR_PAGE,
        cpu_memory::MemoryAccessKind::READ));
    EXPECT_THAT(
        mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING0,
            TEST_PAGING_SECOND_LINEAR_PAGE,
            cpu_memory::MemoryAccessKind::WRITE),
        Eq(TEST_PAGING_SECOND_PHYSICAL_PAGE));

    paging_state.set_write_protect_enabled(true);
    mmu.tlb().clear();
    static_cast<void>(mmu.translate(
        memory_bus,
        paging_state,
        cpu_system::PrivilegeLevel::RING3,
        cpu::Address64{0x7000},
        cpu_memory::MemoryAccessKind::READ));
    try
    {
        static_cast<void>(mmu.translate(
            memory_bus,
            paging_state,
            cpu_system::PrivilegeLevel::RING3,
            cpu::Address64{0x7000},
            cpu_memory::MemoryAccessKind::EXECUTE));
        FAIL() << "expected cached execute-disable page fault";
    }
    catch (const cpu_memory::PageFault& fault)
    {
        EXPECT_THAT(fault.reason(), Eq(cpu_memory::PageFaultReason::EXECUTE_DISABLE));
    }

    mmu.check_access_range(
        memory_bus,
        paging_state,
        cpu_system::PrivilegeLevel::RING3,
        TEST_PAGING_LINEAR_ADDRESS,
        std::size_t{0},
        cpu_memory::MemoryAccessKind::READ);
}
