#pragma once

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/paging.hpp>

namespace mnos::cpu::memory
{
class PageTableBuilder final
{
public:
    PageTableBuilder(MemoryBus& memory_bus, Address64 root_table_address, Address64 next_free_table_address);

    void clear_root_table();
    void map_4k(Address64 linear_address, Address64 physical_address, PagePermissions permissions);
    void map_2m(Address64 linear_address, Address64 physical_address, PagePermissions permissions);
    void map_1g(Address64 linear_address, Address64 physical_address, PagePermissions permissions);

    [[nodiscard]] Address64 root_table_address() const noexcept;
    [[nodiscard]] Address64 next_free_table_address() const noexcept;

private:
    [[nodiscard]] Address64 allocate_table();
    void clear_table(Address64 table_address);
    [[nodiscard]] Address64 ensure_child_table(Address64 table_address, PageTableLevel level, std::size_t entry_index);
    void write_leaf_entry(
        Address64 table_address,
        PageTableLevel level,
        std::size_t entry_index,
        PageTableEntry entry);
    void write_entry(Address64 table_address, std::size_t entry_index, PageTableEntry entry);
    void require_aligned_address(Address64 address, Address64 page_size_bytes) const;

    MemoryBus* memory_bus_;
    Address64 root_table_address_;
    Address64 next_free_table_address_;
};
}
