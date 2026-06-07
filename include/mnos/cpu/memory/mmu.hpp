#pragma once

#include <cstddef>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/page_table_walker.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/memory/tlb.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace mnos::cpu::memory
{
class MemoryManagementUnit final
{
public:
    [[nodiscard]] TranslationLookasideBuffer& tlb() noexcept;
    [[nodiscard]] const TranslationLookasideBuffer& tlb() const noexcept;
    void flush_tlb() noexcept;
    void flush_tlb_context(ProcessContextId context_id) noexcept;
    void invalidate_page(Address64 linear_address) noexcept;
    void invalidate_page(Address64 linear_address, ProcessContextId context_id) noexcept;

    [[nodiscard]] Address64 translate(
        MemoryBus& memory_bus,
        PagingState& paging_state,
        system::PrivilegeLevel privilege_level,
        Address64 linear_address,
        MemoryAccessKind access_kind);

    [[nodiscard]] Qword read(
        MemoryBus& memory_bus,
        PagingState& paging_state,
        system::PrivilegeLevel privilege_level,
        Address64 linear_address,
        DataSize data_size);

    void write(
        MemoryBus& memory_bus,
        PagingState& paging_state,
        system::PrivilegeLevel privilege_level,
        Address64 linear_address,
        DataSize data_size,
        Qword value);

    void check_access_range(
        MemoryBus& memory_bus,
        PagingState& paging_state,
        system::PrivilegeLevel privilege_level,
        Address64 linear_address,
        std::size_t byte_count,
        MemoryAccessKind access_kind);

private:
    [[nodiscard]] Address64 translate_enabled(
        MemoryBus& memory_bus,
        PagingState& paging_state,
        system::PrivilegeLevel privilege_level,
        Address64 linear_address,
        MemoryAccessKind access_kind);
    void ensure_cached_access_allowed(
        const PagingState& paging_state,
        const PageTranslation& translation,
        system::PrivilegeLevel privilege_level,
        Address64 linear_address,
        MemoryAccessKind access_kind) const;
    void mark_cached_translation_dirty(MemoryBus& memory_bus, PageTranslation& translation) const;

    PageTableWalker page_table_walker_;
    TranslationLookasideBuffer tlb_;
};
}
