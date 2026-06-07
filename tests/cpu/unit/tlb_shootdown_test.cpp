#include <optional>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/memory/tlb.hpp>
#include <mnos/cpu/memory/tlb_shootdown.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr cpu::Address64 TEST_ROOT_TABLE = cpu::Address64{0x1000};
constexpr cpu::Address64 TEST_SECOND_ROOT_TABLE = cpu::Address64{0x2000};
constexpr cpu::Address64 TEST_UNALIGNED_ROOT_TABLE = cpu::Address64{0x2001};
constexpr cpu::Address64 TEST_PAGE_A = cpu::Address64{0x4000};
constexpr cpu::Address64 TEST_PAGE_B = cpu::Address64{0x8000};
constexpr cpu::Address64 TEST_PHYSICAL_A = cpu::Address64{0x14000};
constexpr cpu::Address64 TEST_PHYSICAL_B = cpu::Address64{0x18000};
constexpr cpu::Address64 TEST_LEAF_A = cpu::Address64{0x24000};
constexpr cpu::Address64 TEST_LEAF_B = cpu::Address64{0x28000};
constexpr cpu::Qword TEST_GENERATION = cpu::Qword{5};
constexpr cpu_memory::ProcessContextId TEST_PCID{7};
constexpr cpu_memory::ProcessContextId TEST_OTHER_PCID{8};
constexpr cpu_system::CoreId TEST_SOURCE_CORE{0};
constexpr cpu_system::CoreId TEST_TARGET_CORE{1};

[[nodiscard]] cpu_memory::PageTranslation make_translation(
    const cpu::Address64 virtual_page_base,
    const cpu::Address64 physical_frame_base,
    const cpu::Address64 leaf_entry_address)
{
    return cpu_memory::PageTranslation{
        virtual_page_base,
        physical_frame_base,
        cpu_memory::PAGE_SIZE_4K_BYTES,
        cpu_memory::PagePermissions::kernel_read_write_execute(),
        leaf_entry_address,
        false};
}
}

TEST(PagingPcidTest, TagsAddressSpacesAndPreservesGenerationWhenRequested)
{
    cpu_memory::PagingState paging_state;
    EXPECT_FALSE(paging_state.process_context_id_enabled());
    EXPECT_THAT(paging_state.process_context_id(), Eq(cpu_memory::ProcessContextId::kernel()));

    paging_state.load_cr3(TEST_ROOT_TABLE);
    const cpu::Qword kernel_generation = paging_state.generation();
    EXPECT_THROW(
        paging_state.load_cr3(
            TEST_SECOND_ROOT_TABLE,
            TEST_PCID,
            cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT),
        std::logic_error);

    paging_state.set_process_context_id_enabled(true);
    const cpu::Qword pcid_enabled_generation = paging_state.generation();
    paging_state.load_cr3(TEST_SECOND_ROOT_TABLE, TEST_PCID, cpu_memory::Cr3TlbFlushMode::PRESERVE_CONTEXT);
    EXPECT_THAT(paging_state.generation(), Eq(pcid_enabled_generation));
    EXPECT_THAT(paging_state.process_context_id(), Eq(TEST_PCID));

    paging_state.load_cr3(TEST_ROOT_TABLE, TEST_PCID, cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    EXPECT_GT(paging_state.generation(), pcid_enabled_generation);
    EXPECT_GT(paging_state.generation(), kernel_generation);
    EXPECT_THROW(paging_state.load_cr3(TEST_UNALIGNED_ROOT_TABLE), std::out_of_range);
    EXPECT_THROW(
        paging_state.load_cr3(
            TEST_ROOT_TABLE,
            cpu_memory::ProcessContextId{
                static_cast<cpu_memory::ProcessContextId::value_type>(cpu_memory::PROCESS_CONTEXT_ID_MAX_VALUE + 1U)},
            cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT),
        std::out_of_range);

    paging_state.set_process_context_id_enabled(false);
    EXPECT_FALSE(paging_state.process_context_id_enabled());
    EXPECT_THAT(paging_state.process_context_id(), Eq(cpu_memory::ProcessContextId::kernel()));
}

TEST(TlbPcidTest, InvalidatesPagesByProcessContextId)
{
    cpu_memory::TranslationLookasideBuffer tlb;
    tlb.insert(make_translation(TEST_PAGE_A, TEST_PHYSICAL_A, TEST_LEAF_A), TEST_GENERATION, TEST_PCID);

    EXPECT_NE(tlb.lookup(TEST_PAGE_A, TEST_GENERATION, TEST_PCID), nullptr);
    EXPECT_EQ(tlb.lookup(TEST_PAGE_A, TEST_GENERATION, TEST_OTHER_PCID), nullptr);
    EXPECT_EQ(tlb.lookup(TEST_PAGE_A, TEST_GENERATION), nullptr);

    tlb.invalidate_page(TEST_PAGE_A, TEST_OTHER_PCID);
    EXPECT_THAT(tlb.size(), Eq(std::size_t{1}));
    tlb.invalidate_context(TEST_OTHER_PCID);
    EXPECT_THAT(tlb.size(), Eq(std::size_t{1}));

    tlb.invalidate_page(TEST_PAGE_A, TEST_PCID);
    EXPECT_TRUE(tlb.empty());
}

TEST(TlbShootdownControllerTest, AppliesPageAndContextShootdownsToTargetMmu)
{
    cpu_memory::MemoryManagementUnit target_mmu;
    target_mmu.tlb().insert(make_translation(TEST_PAGE_A, TEST_PHYSICAL_A, TEST_LEAF_A), TEST_GENERATION, TEST_PCID);
    target_mmu.tlb().insert(
        make_translation(TEST_PAGE_B, TEST_PHYSICAL_B, TEST_LEAF_B),
        TEST_GENERATION,
        TEST_OTHER_PCID);

    cpu_memory::TlbShootdownController controller;
    const cpu_memory::TlbShootdownRequest& page_request =
        controller.request_page(TEST_SOURCE_CORE, TEST_TARGET_CORE, TEST_PAGE_A, TEST_PCID);
    EXPECT_THAT(page_request.scope(), Eq(cpu_memory::TlbShootdownScope::PAGE));
    EXPECT_THAT(page_request.linear_address(), Eq(TEST_PAGE_A));
    const cpu::Qword page_sequence = page_request.sequence();
    EXPECT_TRUE(controller.has_pending_for(TEST_TARGET_CORE));

    const std::optional<cpu_memory::TlbShootdownRequest> pending_page_request =
        controller.take_next_for(TEST_TARGET_CORE);
    ASSERT_TRUE(pending_page_request.has_value());
    controller.apply(target_mmu, pending_page_request.value());
    EXPECT_TRUE(controller.has_acknowledged(page_sequence));
    EXPECT_EQ(target_mmu.tlb().lookup(TEST_PAGE_A, TEST_GENERATION, TEST_PCID), nullptr);
    EXPECT_NE(target_mmu.tlb().lookup(TEST_PAGE_B, TEST_GENERATION, TEST_OTHER_PCID), nullptr);

    const cpu_memory::TlbShootdownRequest& context_request =
        controller.request_all(TEST_SOURCE_CORE, TEST_TARGET_CORE, TEST_OTHER_PCID);
    EXPECT_THAT(context_request.scope(), Eq(cpu_memory::TlbShootdownScope::ALL));
    EXPECT_THROW(static_cast<void>(context_request.linear_address()), std::logic_error);
    const cpu::Qword context_sequence = context_request.sequence();
    const std::optional<cpu_memory::TlbShootdownRequest> pending_context_request =
        controller.take_next_for(TEST_TARGET_CORE);
    ASSERT_TRUE(pending_context_request.has_value());
    controller.apply(target_mmu, pending_context_request.value());
    EXPECT_TRUE(controller.has_acknowledged(context_sequence));
    EXPECT_TRUE(target_mmu.tlb().empty());
}

TEST(TlbShootdownControllerTest, SupportsGlobalFlushAndMissingOptionalFields)
{
    cpu_memory::MemoryManagementUnit target_mmu;
    target_mmu.tlb().insert(make_translation(TEST_PAGE_A, TEST_PHYSICAL_A, TEST_LEAF_A), TEST_GENERATION, TEST_PCID);

    cpu_memory::TlbShootdownController controller;
    static_cast<void>(controller.request_all(TEST_SOURCE_CORE, TEST_TARGET_CORE));
    const std::optional<cpu_memory::TlbShootdownRequest> request = controller.take_next_for(TEST_TARGET_CORE);
    ASSERT_TRUE(request.has_value());
    EXPECT_FALSE(request->has_process_context_id());
    EXPECT_THROW(static_cast<void>(request->process_context_id()), std::logic_error);
    controller.apply(target_mmu, request.value());

    EXPECT_TRUE(target_mmu.tlb().empty());
    EXPECT_THAT(controller.acknowledged_count(), Eq(std::size_t{1}));
    EXPECT_FALSE(controller.take_next_for(cpu_system::CoreId{2}).has_value());
}
