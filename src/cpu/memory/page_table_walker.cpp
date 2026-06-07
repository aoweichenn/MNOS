#include <array>
#include <stdexcept>

#include <mnos/cpu/memory/page_table_walker.hpp>

namespace
{
constexpr const char* PAGE_TABLE_WALK_NON_CANONICAL_ADDRESS_MESSAGE =
    "x86-64 linear address is not canonical";

struct EffectivePermissions
{
    bool writable = true;
    bool user_accessible = true;
    bool executable = true;

    void apply(const mnos::cpu::memory::PageTableEntry& entry) noexcept
    {
        this->writable = this->writable && entry.is_writable();
        this->user_accessible = this->user_accessible && entry.is_user_accessible();
        this->executable = this->executable && !entry.is_no_execute();
    }
};

[[nodiscard]] mnos::cpu::memory::PageFault make_page_fault(
    const mnos::cpu::Address64 linear_address,
    const mnos::cpu::memory::MemoryAccessKind access_kind,
    const mnos::cpu::system::PrivilegeLevel privilege_level,
    const mnos::cpu::memory::PageFaultReason reason)
{
    return mnos::cpu::memory::PageFault{
        linear_address,
        access_kind,
        reason,
        mnos::cpu::memory::make_page_fault_error_code(access_kind, privilege_level, reason)};
}

[[noreturn]] void throw_page_fault(
    const mnos::cpu::Address64 linear_address,
    const mnos::cpu::memory::MemoryAccessKind access_kind,
    const mnos::cpu::system::PrivilegeLevel privilege_level,
    const mnos::cpu::memory::PageFaultReason reason)
{
    throw make_page_fault(linear_address, access_kind, privilege_level, reason);
}

[[nodiscard]] bool is_user_access(const mnos::cpu::system::PrivilegeLevel privilege_level) noexcept
{
    return privilege_level == mnos::cpu::system::PrivilegeLevel::RING3;
}

[[nodiscard]] bool is_write_allowed_for_read_only_page(
    const mnos::cpu::memory::PagingState& paging_state,
    const mnos::cpu::system::PrivilegeLevel privilege_level) noexcept
{
    return privilege_level != mnos::cpu::system::PrivilegeLevel::RING3 && !paging_state.write_protect_enabled();
}

void enforce_permissions(
    const mnos::cpu::memory::PagingState& paging_state,
    const EffectivePermissions permissions,
    const mnos::cpu::Address64 linear_address,
    const mnos::cpu::memory::MemoryAccessKind access_kind,
    const mnos::cpu::system::PrivilegeLevel privilege_level)
{
    if (is_user_access(privilege_level) && !permissions.user_accessible)
    {
        throw_page_fault(
            linear_address,
            access_kind,
            privilege_level,
            mnos::cpu::memory::PageFaultReason::USER_SUPERVISOR);
    }

    if (access_kind == mnos::cpu::memory::MemoryAccessKind::WRITE && !permissions.writable &&
        !is_write_allowed_for_read_only_page(paging_state, privilege_level))
    {
        throw_page_fault(
            linear_address,
            access_kind,
            privilege_level,
            mnos::cpu::memory::PageFaultReason::WRITE_PROTECTION);
    }

    if (access_kind == mnos::cpu::memory::MemoryAccessKind::EXECUTE && !permissions.executable)
    {
        throw_page_fault(
            linear_address,
            access_kind,
            privilege_level,
            mnos::cpu::memory::PageFaultReason::EXECUTE_DISABLE);
    }
}

[[nodiscard]] bool is_large_leaf_entry(
    const mnos::cpu::memory::PageTableEntry& entry,
    const mnos::cpu::memory::PageTableLevel level) noexcept
{
    return (level == mnos::cpu::memory::PageTableLevel::PD ||
            level == mnos::cpu::memory::PageTableLevel::PDPT) &&
        entry.is_huge_page();
}

[[nodiscard]] bool is_leaf_entry(
    const mnos::cpu::memory::PageTableEntry& entry,
    const mnos::cpu::memory::PageTableLevel level) noexcept
{
    return level == mnos::cpu::memory::PageTableLevel::PT || is_large_leaf_entry(entry, level);
}

void validate_reserved_bits(
    const mnos::cpu::memory::PagingState& paging_state,
    const mnos::cpu::memory::PageTableEntry& entry,
    const mnos::cpu::memory::PageTableLevel level,
    const mnos::cpu::Address64 linear_address,
    const mnos::cpu::memory::MemoryAccessKind access_kind,
    const mnos::cpu::system::PrivilegeLevel privilege_level)
{
    const bool huge_bit_is_reserved = level == mnos::cpu::memory::PageTableLevel::PML4;
    if (entry.has_reserved_high_bits() || (!paging_state.execute_disable_enabled() && entry.is_no_execute()) ||
        (huge_bit_is_reserved && entry.is_huge_page()))
    {
        throw_page_fault(
            linear_address,
            access_kind,
            privilege_level,
            mnos::cpu::memory::PageFaultReason::RESERVED_BIT);
    }

    if (is_large_leaf_entry(entry, level))
    {
        const mnos::cpu::Address64 page_size = mnos::cpu::memory::page_size_for_leaf_level(level);
        if ((entry.table_address() & (page_size - mnos::cpu::Address64{1})) != mnos::cpu::Address64{0})
        {
            throw_page_fault(
                linear_address,
                access_kind,
                privilege_level,
                mnos::cpu::memory::PageFaultReason::RESERVED_BIT);
        }
    }
}

[[nodiscard]] mnos::cpu::memory::PageTableEntry read_page_table_entry(
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::cpu::Address64 entry_address)
{
    return mnos::cpu::memory::PageTableEntry{memory_bus.read(entry_address, mnos::cpu::DataSize::QWORD)};
}

void write_page_table_entry_if_changed(
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::cpu::Address64 entry_address,
    const mnos::cpu::memory::PageTableEntry original_entry,
    const mnos::cpu::memory::PageTableEntry updated_entry)
{
    if (updated_entry.raw_bits() != original_entry.raw_bits())
    {
        memory_bus.write(entry_address, mnos::cpu::DataSize::QWORD, updated_entry.raw_bits());
    }
}

[[nodiscard]] mnos::cpu::memory::PageTableEntry mark_accessed_dirty(
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::cpu::Address64 entry_address,
    const mnos::cpu::memory::PageTableEntry entry,
    const bool leaf,
    const mnos::cpu::memory::MemoryAccessKind access_kind)
{
    mnos::cpu::memory::PageTableEntry updated_entry = entry.with_accessed();
    if (leaf && access_kind == mnos::cpu::memory::MemoryAccessKind::WRITE)
    {
        updated_entry = updated_entry.with_dirty();
    }
    write_page_table_entry_if_changed(memory_bus, entry_address, entry, updated_entry);
    return updated_entry;
}

[[nodiscard]] mnos::cpu::Address64 entry_address_for_index(
    const mnos::cpu::Address64 table_address,
    const std::size_t entry_index) noexcept
{
    return table_address +
        (static_cast<mnos::cpu::Address64>(entry_index) * mnos::cpu::memory::PAGE_TABLE_ENTRY_BYTES);
}
}

namespace mnos::cpu::memory
{
PageTranslation PageTableWalker::translate(
    MemoryBus& memory_bus,
    const PagingState& paging_state,
    const Address64 linear_address,
    const MemoryAccessKind access_kind,
    const system::PrivilegeLevel privilege_level) const
{
    if (!is_canonical_linear_address(linear_address))
    {
        throw std::out_of_range{PAGE_TABLE_WALK_NON_CANONICAL_ADDRESS_MESSAGE};
    }

    static constexpr std::array<PageTableLevel, PAGE_TABLE_LEVEL_COUNT> WALK_ORDER{
        PageTableLevel::PML4,
        PageTableLevel::PDPT,
        PageTableLevel::PD,
        PageTableLevel::PT};

    Address64 table_address = paging_state.cr3();
    EffectivePermissions permissions;

    for (const PageTableLevel level : WALK_ORDER)
    {
        const std::size_t entry_index = page_table_index(linear_address, level);
        const Address64 entry_address = entry_address_for_index(table_address, entry_index);
        const PageTableEntry entry = read_page_table_entry(memory_bus, entry_address);
        if (!entry.is_present())
        {
            throw_page_fault(
                linear_address,
                access_kind,
                privilege_level,
                PageFaultReason::NOT_PRESENT);
        }

        validate_reserved_bits(paging_state, entry, level, linear_address, access_kind, privilege_level);
        permissions.apply(entry);

        const bool leaf = is_leaf_entry(entry, level);
        if (leaf)
        {
            enforce_permissions(paging_state, permissions, linear_address, access_kind, privilege_level);
            const PageTableEntry updated_entry =
                mark_accessed_dirty(memory_bus, entry_address, entry, leaf, access_kind);
            const Address64 page_size = page_size_for_leaf_level(level);
            return PageTranslation{
                page_base(linear_address, page_size),
                updated_entry.frame_address(page_size),
                page_size,
                PagePermissions{permissions.writable, permissions.user_accessible, permissions.executable},
                entry_address,
                updated_entry.is_dirty()};
        }

        static_cast<void>(mark_accessed_dirty(memory_bus, entry_address, entry, leaf, access_kind));
        table_address = entry.table_address();
    }

    throw_page_fault(linear_address, access_kind, privilege_level, PageFaultReason::NOT_PRESENT);
}
}
