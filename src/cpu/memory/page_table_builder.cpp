#include <stdexcept>

#include <mnos/cpu/memory/page_table_builder.hpp>

namespace
{
constexpr const char* PAGE_TABLE_BUILDER_ALIGNED_ADDRESS_MESSAGE =
    "page table builder address is not aligned to the required page size";
constexpr const char* PAGE_TABLE_BUILDER_TABLE_SPACE_MESSAGE =
    "page table builder cannot allocate a page table outside physical memory";
constexpr const char* PAGE_TABLE_BUILDER_LEAF_CONFLICT_MESSAGE =
    "page table builder cannot split an existing large-page mapping";
constexpr const char* PAGE_TABLE_BUILDER_CHILD_TABLE_CONFLICT_MESSAGE =
    "page table builder cannot replace an existing child table with a leaf mapping";
constexpr const char* PAGE_TABLE_BUILDER_ARENA_ORDER_MESSAGE =
    "page table builder arena end must be after the next free table address";

[[nodiscard]] mnos::cpu::Address64 entry_address_for_index(
    const mnos::cpu::Address64 table_address,
    const std::size_t entry_index) noexcept
{
    return table_address +
        (static_cast<mnos::cpu::Address64>(entry_index) * mnos::cpu::memory::PAGE_TABLE_ENTRY_BYTES);
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

[[nodiscard]] mnos::cpu::memory::PageTableEntry read_entry(
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::cpu::Address64 entry_address)
{
    return mnos::cpu::memory::PageTableEntry{memory_bus.read(entry_address, mnos::cpu::DataSize::QWORD)};
}
}

namespace mnos::cpu::memory
{
PageTableBuilder::PageTableBuilder(
    MemoryBus& memory_bus,
    const Address64 root_table_address,
    const Address64 next_free_table_address) :
    PageTableBuilder(memory_bus, root_table_address, next_free_table_address, Address64{0})
{
}

PageTableBuilder::PageTableBuilder(
    MemoryBus& memory_bus,
    const Address64 root_table_address,
    const Address64 next_free_table_address,
    const Address64 table_arena_end_address) :
    memory_bus_(&memory_bus),
    root_table_address_(root_table_address),
    next_free_table_address_(next_free_table_address),
    table_arena_end_address_(table_arena_end_address)
{
    this->require_aligned_address(root_table_address, PAGE_SIZE_4K_BYTES);
    this->require_aligned_address(next_free_table_address, PAGE_SIZE_4K_BYTES);
    if (this->table_arena_end_address_ != Address64{0})
    {
        this->require_aligned_address(this->table_arena_end_address_, PAGE_SIZE_4K_BYTES);
        if (this->next_free_table_address_ > this->table_arena_end_address_)
        {
            throw std::out_of_range{PAGE_TABLE_BUILDER_ARENA_ORDER_MESSAGE};
        }
    }
}

void PageTableBuilder::clear_root_table()
{
    this->clear_table(this->root_table_address_);
}

void PageTableBuilder::map_4k(
    const Address64 linear_address,
    const Address64 physical_address,
    const PagePermissions permissions)
{
    this->require_aligned_address(linear_address, PAGE_SIZE_4K_BYTES);
    this->require_aligned_address(physical_address, PAGE_SIZE_4K_BYTES);

    const Address64 pdpt_table = this->ensure_child_table(
        this->root_table_address_,
        PageTableLevel::PML4,
        page_table_index(linear_address, PageTableLevel::PML4));
    const Address64 pd_table = this->ensure_child_table(
        pdpt_table,
        PageTableLevel::PDPT,
        page_table_index(linear_address, PageTableLevel::PDPT));
    const Address64 pt_table = this->ensure_child_table(
        pd_table,
        PageTableLevel::PD,
        page_table_index(linear_address, PageTableLevel::PD));
    this->write_leaf_entry(
        pt_table,
        PageTableLevel::PT,
        page_table_index(linear_address, PageTableLevel::PT),
        PageTableEntry::page_4k(physical_address, permissions));
}

void PageTableBuilder::map_2m(
    const Address64 linear_address,
    const Address64 physical_address,
    const PagePermissions permissions)
{
    this->require_aligned_address(linear_address, PAGE_SIZE_2M_BYTES);
    this->require_aligned_address(physical_address, PAGE_SIZE_2M_BYTES);

    const Address64 pdpt_table = this->ensure_child_table(
        this->root_table_address_,
        PageTableLevel::PML4,
        page_table_index(linear_address, PageTableLevel::PML4));
    const Address64 pd_table = this->ensure_child_table(
        pdpt_table,
        PageTableLevel::PDPT,
        page_table_index(linear_address, PageTableLevel::PDPT));
    this->write_leaf_entry(
        pd_table,
        PageTableLevel::PD,
        page_table_index(linear_address, PageTableLevel::PD),
        PageTableEntry::page_2m(physical_address, permissions));
}

void PageTableBuilder::map_1g(
    const Address64 linear_address,
    const Address64 physical_address,
    const PagePermissions permissions)
{
    this->require_aligned_address(linear_address, PAGE_SIZE_1G_BYTES);
    this->require_aligned_address(physical_address, PAGE_SIZE_1G_BYTES);

    const Address64 pdpt_table = this->ensure_child_table(
        this->root_table_address_,
        PageTableLevel::PML4,
        page_table_index(linear_address, PageTableLevel::PML4));
    this->write_leaf_entry(
        pdpt_table,
        PageTableLevel::PDPT,
        page_table_index(linear_address, PageTableLevel::PDPT),
        PageTableEntry::page_1g(physical_address, permissions));
}

Address64 PageTableBuilder::root_table_address() const noexcept
{
    return this->root_table_address_;
}

Address64 PageTableBuilder::next_free_table_address() const noexcept
{
    return this->next_free_table_address_;
}

Address64 PageTableBuilder::allocate_table()
{
    if (!this->memory_bus_->contains_range(this->next_free_table_address_, static_cast<std::size_t>(PAGE_SIZE_4K_BYTES)))
    {
        throw std::out_of_range{PAGE_TABLE_BUILDER_TABLE_SPACE_MESSAGE};
    }

    if (this->table_arena_end_address_ != Address64{0} &&
        this->next_free_table_address_ > this->table_arena_end_address_ - PAGE_SIZE_4K_BYTES)
    {
        throw std::out_of_range{PAGE_TABLE_BUILDER_TABLE_SPACE_MESSAGE};
    }

    const Address64 table_address = this->next_free_table_address_;
    this->next_free_table_address_ += PAGE_SIZE_4K_BYTES;
    this->clear_table(table_address);
    return table_address;
}

void PageTableBuilder::clear_table(const Address64 table_address)
{
    for (std::size_t entry_index = 0; entry_index < PAGE_TABLE_ENTRY_COUNT; ++entry_index)
    {
        this->memory_bus_->write(entry_address_for_index(table_address, entry_index), DataSize::QWORD, Qword{0});
    }
}

Address64 PageTableBuilder::ensure_child_table(
    const Address64 table_address,
    const PageTableLevel level,
    const std::size_t entry_index)
{
    const Address64 entry_address = entry_address_for_index(table_address, entry_index);
    const PageTableEntry existing_entry = read_entry(*this->memory_bus_, entry_address);
    if (existing_entry.is_present())
    {
        if (is_large_leaf_entry(existing_entry, level))
        {
            throw std::logic_error{PAGE_TABLE_BUILDER_LEAF_CONFLICT_MESSAGE};
        }
        return existing_entry.table_address();
    }

    const Address64 child_table_address = this->allocate_table();
    this->memory_bus_->write(
        entry_address,
        DataSize::QWORD,
        PageTableEntry::table(child_table_address, PagePermissions::user_read_write_execute()).raw_bits());
    return child_table_address;
}

void PageTableBuilder::write_entry(
    const Address64 table_address,
    const std::size_t entry_index,
    const PageTableEntry entry)
{
    this->memory_bus_->write(entry_address_for_index(table_address, entry_index), DataSize::QWORD, entry.raw_bits());
}

void PageTableBuilder::write_leaf_entry(
    const Address64 table_address,
    const PageTableLevel level,
    const std::size_t entry_index,
    const PageTableEntry entry)
{
    const Address64 entry_address = entry_address_for_index(table_address, entry_index);
    const PageTableEntry existing_entry = read_entry(*this->memory_bus_, entry_address);
    if (existing_entry.is_present() && !is_leaf_entry(existing_entry, level))
    {
        throw std::logic_error{PAGE_TABLE_BUILDER_CHILD_TABLE_CONFLICT_MESSAGE};
    }

    this->write_entry(table_address, entry_index, entry);
}

void PageTableBuilder::require_aligned_address(const Address64 address, const Address64 page_size_bytes) const
{
    if (!is_aligned_to_page_size(address, page_size_bytes))
    {
        throw std::out_of_range{PAGE_TABLE_BUILDER_ALIGNED_ADDRESS_MESSAGE};
    }
}
}
