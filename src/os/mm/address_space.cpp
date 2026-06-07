#include <limits>
#include <stdexcept>

#include <mnos/cpu/memory/page_table_builder.hpp>
#include <mnos/os/mm/address_space.hpp>

namespace
{
constexpr const char* ADDRESS_SPACE_UNALIGNED_VIRTUAL_ADDRESS_MESSAGE =
    "address space virtual address must be page aligned";
constexpr const char* ADDRESS_SPACE_UNALIGNED_PHYSICAL_ADDRESS_MESSAGE =
    "address space physical address must be page aligned";
constexpr const char* ADDRESS_SPACE_UNALIGNED_BYTE_COUNT_MESSAGE =
    "address space mapping byte count must be a page multiple";
constexpr const char* ADDRESS_SPACE_EMPTY_RANGE_MESSAGE =
    "address space mapping range must not be empty";
constexpr const char* ADDRESS_SPACE_TABLE_ARENA_ORDER_MESSAGE =
    "address space table arena addresses are not ordered";
constexpr const char* ADDRESS_SPACE_ROOT_OUT_OF_RANGE_MESSAGE =
    "address space root table is outside physical memory";
constexpr const char* ADDRESS_SPACE_VIRTUAL_RANGE_OVERFLOW_MESSAGE =
    "address space virtual mapping range overflows address value";
constexpr const char* ADDRESS_SPACE_PHYSICAL_RANGE_OVERFLOW_MESSAGE =
    "address space physical mapping range overflows address value";

[[nodiscard]] std::size_t page_size_as_size_t() noexcept
{
    return static_cast<std::size_t>(mnos::os::mm::MM_PAGE_SIZE_BYTES);
}

[[nodiscard]] bool range_exceeds_address_space(
    const mnos::os::mm::AddressValue start_address,
    const mnos::os::mm::AddressValue byte_count) noexcept
{
    return start_address >
        std::numeric_limits<mnos::os::mm::AddressValue>::max() - (byte_count - mnos::os::mm::AddressValue{1});
}
}

namespace mnos::os::mm
{
AddressSpace::AddressSpace(
    cpu::MemoryBus& memory_bus,
    const PhysicalAddress root_table_address,
    const PhysicalAddress next_free_table_address,
    const PhysicalAddress table_arena_end_address) :
    memory_bus_(&memory_bus),
    root_table_address_(root_table_address),
    initial_next_free_table_address_(next_free_table_address),
    next_free_table_address_(next_free_table_address),
    table_arena_end_address_(table_arena_end_address)
{
    this->require_page_aligned(this->root_table_address_);
    this->require_page_aligned(this->next_free_table_address_);
    this->require_page_aligned(this->table_arena_end_address_);

    if (this->root_table_address_ >= this->table_arena_end_address_ ||
        this->next_free_table_address_ > this->table_arena_end_address_ ||
        this->next_free_table_address_ < this->root_table_address_)
    {
        throw std::out_of_range{ADDRESS_SPACE_TABLE_ARENA_ORDER_MESSAGE};
    }

    if (!this->memory_bus_->contains_range(to_cpu_address(this->root_table_address_), page_size_as_size_t()))
    {
        throw std::out_of_range{ADDRESS_SPACE_ROOT_OUT_OF_RANGE_MESSAGE};
    }

    this->clear_root_table();
}

cpu::MemoryBus& AddressSpace::memory_bus() noexcept
{
    return *this->memory_bus_;
}

const cpu::MemoryBus& AddressSpace::memory_bus() const noexcept
{
    return *this->memory_bus_;
}

PhysicalAddress AddressSpace::root_table_address() const noexcept
{
    return this->root_table_address_;
}

PhysicalAddress AddressSpace::next_free_table_address() const noexcept
{
    return this->next_free_table_address_;
}

PhysicalAddress AddressSpace::table_arena_end_address() const noexcept
{
    return this->table_arena_end_address_;
}

bool AddressSpace::has_available_table_pages() const noexcept
{
    return this->next_free_table_address_ < this->table_arena_end_address_;
}

void AddressSpace::clear_root_table()
{
    this->next_free_table_address_ = this->initial_next_free_table_address_;
    cpu::memory::PageTableBuilder builder{
        *this->memory_bus_,
        to_cpu_address(this->root_table_address_),
        to_cpu_address(this->next_free_table_address_),
        to_cpu_address(this->table_arena_end_address_)};
    builder.clear_root_table();
}

void AddressSpace::map_page(
    const VirtualAddress virtual_address,
    const PhysicalAddress physical_address,
    const cpu::memory::PagePermissions permissions)
{
    this->require_page_aligned(virtual_address);
    this->require_page_aligned(physical_address);

    cpu::memory::PageTableBuilder builder{
        *this->memory_bus_,
        to_cpu_address(this->root_table_address_),
        to_cpu_address(this->next_free_table_address_),
        to_cpu_address(this->table_arena_end_address_)};
    builder.map_4k(to_cpu_address(virtual_address), to_cpu_address(physical_address), permissions);
    this->update_next_free_table_address(builder);
}

void AddressSpace::map_range(
    VirtualAddress virtual_address,
    PhysicalAddress physical_address,
    const AddressValue byte_count,
    const cpu::memory::PagePermissions permissions)
{
    this->require_page_aligned(virtual_address);
    this->require_page_aligned(physical_address);
    this->require_page_multiple(byte_count);
    if (range_exceeds_address_space(virtual_address.value(), byte_count))
    {
        throw std::overflow_error{ADDRESS_SPACE_VIRTUAL_RANGE_OVERFLOW_MESSAGE};
    }
    if (range_exceeds_address_space(physical_address.value(), byte_count))
    {
        throw std::overflow_error{ADDRESS_SPACE_PHYSICAL_RANGE_OVERFLOW_MESSAGE};
    }

    const PageNumber page_count = page_count_for_bytes(byte_count);
    for (PageNumber page_index = PageNumber{0}; page_index < page_count; ++page_index)
    {
        const AddressValue page_offset = page_index * MM_PAGE_SIZE_BYTES;
        this->map_page(
            virtual_address + page_offset,
            physical_address + page_offset,
            permissions);
    }
}

void AddressSpace::identity_map_range(
    const PhysicalAddress physical_address,
    const AddressValue byte_count,
    const cpu::memory::PagePermissions permissions)
{
    this->map_range(VirtualAddress{physical_address.value()}, physical_address, byte_count, permissions);
}

void AddressSpace::activate(cpu::CpuState& cpu_state) const
{
    cpu_state.paging().load_cr3(to_cpu_address(this->root_table_address_));
    cpu_state.paging().enable();
}

void AddressSpace::require_page_aligned(const VirtualAddress address) const
{
    if (!is_page_aligned(address))
    {
        throw std::invalid_argument{ADDRESS_SPACE_UNALIGNED_VIRTUAL_ADDRESS_MESSAGE};
    }
}

void AddressSpace::require_page_aligned(const PhysicalAddress address) const
{
    if (!is_page_aligned(address))
    {
        throw std::invalid_argument{ADDRESS_SPACE_UNALIGNED_PHYSICAL_ADDRESS_MESSAGE};
    }
}

void AddressSpace::require_page_multiple(const AddressValue byte_count) const
{
    if (byte_count == AddressValue{0})
    {
        throw std::invalid_argument{ADDRESS_SPACE_EMPTY_RANGE_MESSAGE};
    }

    if (!is_page_aligned(byte_count))
    {
        throw std::invalid_argument{ADDRESS_SPACE_UNALIGNED_BYTE_COUNT_MESSAGE};
    }
}

void AddressSpace::update_next_free_table_address(cpu::memory::PageTableBuilder& builder) noexcept
{
    this->next_free_table_address_ = PhysicalAddress{builder.next_free_table_address()};
}
}
