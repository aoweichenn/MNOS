#pragma once

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/os/mm/address.hpp>
#include <mnos/os/mm/page.hpp>

namespace mnos::cpu::memory
{
class PageTableBuilder;
}

namespace mnos::os::mm
{
class AddressSpace final
{
public:
    AddressSpace(
        cpu::MemoryBus& memory_bus,
        PhysicalAddress root_table_address,
        PhysicalAddress next_free_table_address,
        PhysicalAddress table_arena_end_address);
    AddressSpace(const AddressSpace&) = delete;
    AddressSpace& operator=(const AddressSpace&) = delete;
    AddressSpace(AddressSpace&&) noexcept = default;
    AddressSpace& operator=(AddressSpace&&) noexcept = default;

    [[nodiscard]] cpu::MemoryBus& memory_bus() noexcept;
    [[nodiscard]] const cpu::MemoryBus& memory_bus() const noexcept;
    [[nodiscard]] PhysicalAddress root_table_address() const noexcept;
    [[nodiscard]] PhysicalAddress next_free_table_address() const noexcept;
    [[nodiscard]] PhysicalAddress table_arena_end_address() const noexcept;
    [[nodiscard]] bool has_available_table_pages() const noexcept;

    void clear_root_table();
    void map_page(VirtualAddress virtual_address, PhysicalAddress physical_address, cpu::memory::PagePermissions permissions);
    void map_range(
        VirtualAddress virtual_address,
        PhysicalAddress physical_address,
        AddressValue byte_count,
        cpu::memory::PagePermissions permissions);
    void identity_map_range(
        PhysicalAddress physical_address,
        AddressValue byte_count,
        cpu::memory::PagePermissions permissions);
    void activate(cpu::CpuState& cpu_state) const;

private:
    void require_page_aligned(VirtualAddress address) const;
    void require_page_aligned(PhysicalAddress address) const;
    void require_page_multiple(AddressValue byte_count) const;
    void update_next_free_table_address(cpu::memory::PageTableBuilder& builder) noexcept;

    cpu::MemoryBus* memory_bus_;
    PhysicalAddress root_table_address_;
    PhysicalAddress initial_next_free_table_address_;
    PhysicalAddress next_free_table_address_;
    PhysicalAddress table_arena_end_address_;
};
}
