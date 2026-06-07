#include <array>
#include <stdexcept>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/memory/paging.hpp>

namespace
{
constexpr std::string_view PAGING_INVALID_NAME = "<invalid>";
constexpr const char* PAGING_INVALID_ACCESS_KIND_MESSAGE = "memory access kind is invalid";
constexpr const char* PAGING_INVALID_FAULT_REASON_MESSAGE = "page fault reason is invalid";
constexpr const char* PAGING_INVALID_TABLE_LEVEL_MESSAGE = "page table level is invalid";
constexpr const char* PAGING_PML4_LEAF_MESSAGE = "pml4 cannot be used as a leaf page table level";
constexpr const char* PAGING_ADDRESS_ALIGNMENT_MESSAGE = "paging address is not aligned to the required page size";
constexpr const char* PAGING_CR3_ALIGNMENT_MESSAGE = "paging cr3 root table address must be 4KiB aligned";
constexpr const char* PAGING_PCID_DISABLED_NON_KERNEL_MESSAGE =
    "paging pcid must be enabled before loading a non-kernel process context id";
constexpr const char* PAGING_PCID_OUT_OF_RANGE_MESSAGE = "paging process context id exceeds the x86-64 pcid range";
constexpr const char* PAGING_INVALID_CR3_FLUSH_MODE_MESSAGE = "paging cr3 tlb flush mode is invalid";
constexpr const char* PAGING_PAGE_FAULT_MESSAGE = "x86-64 page fault";
constexpr mnos::cpu::Address64 PAGING_CANONICAL_SIGN_BIT = mnos::cpu::Address64{1} << 47;
constexpr mnos::cpu::Address64 PAGING_CANONICAL_HIGH_MASK = mnos::cpu::Address64{0xFFFF'0000'0000'0000ULL};

class MemoryAccessKindCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::memory::MemoryAccessKind kind) noexcept
    {
        return NAMES.contains(kind);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::memory::MemoryAccessKind kind) noexcept
    {
        return NAMES.index(kind);
    }

    [[nodiscard]] static std::string_view name(const mnos::cpu::memory::MemoryAccessKind kind) noexcept
    {
        return NAMES.name(kind);
    }

private:
    inline static constexpr auto NAMES = mnos::core::make_enum_name_table<mnos::cpu::memory::MemoryAccessKind>(
        std::array<std::string_view, mnos::cpu::memory::MEMORY_ACCESS_KIND_COUNT>{"READ", "WRITE", "EXECUTE"},
        PAGING_INVALID_NAME);
};

class PageFaultReasonCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::memory::PageFaultReason reason) noexcept
    {
        return NAMES.contains(reason);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::memory::PageFaultReason reason) noexcept
    {
        return NAMES.index(reason);
    }

    [[nodiscard]] static std::string_view name(const mnos::cpu::memory::PageFaultReason reason) noexcept
    {
        return NAMES.name(reason);
    }

private:
    inline static constexpr auto NAMES = mnos::core::make_enum_name_table<mnos::cpu::memory::PageFaultReason>(
        std::array<std::string_view, mnos::cpu::memory::PAGE_FAULT_REASON_COUNT>{
            "NOT_PRESENT",
            "WRITE_PROTECTION",
            "USER_SUPERVISOR",
            "EXECUTE_DISABLE",
            "RESERVED_BIT"},
        PAGING_INVALID_NAME);
};

class PageTableLevelCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::memory::PageTableLevel level) noexcept
    {
        return NAMES.contains(level);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::memory::PageTableLevel level) noexcept
    {
        return NAMES.index(level);
    }

    [[nodiscard]] static std::string_view name(const mnos::cpu::memory::PageTableLevel level) noexcept
    {
        return NAMES.name(level);
    }

private:
    inline static constexpr auto NAMES = mnos::core::make_enum_name_table<mnos::cpu::memory::PageTableLevel>(
        std::array<std::string_view, mnos::cpu::memory::PAGE_TABLE_LEVEL_ENUM_COUNT>{"PT", "PD", "PDPT", "PML4"},
        PAGING_INVALID_NAME);
};

void require_page_table_level(const mnos::cpu::memory::PageTableLevel level)
{
    if (!mnos::cpu::memory::is_page_table_level_valid(level))
    {
        throw std::out_of_range{PAGING_INVALID_TABLE_LEVEL_MESSAGE};
    }
}

void require_aligned_address(const mnos::cpu::Address64 address, const mnos::cpu::Address64 page_size_bytes)
{
    if (!mnos::cpu::memory::is_aligned_to_page_size(address, page_size_bytes))
    {
        throw std::out_of_range{PAGING_ADDRESS_ALIGNMENT_MESSAGE};
    }
}
}

namespace mnos::cpu::memory
{
bool is_process_context_id_valid(const ProcessContextId context_id) noexcept
{
    return context_id.value() <= PROCESS_CONTEXT_ID_MAX_VALUE;
}

bool is_memory_access_kind_valid(const MemoryAccessKind kind) noexcept
{
    return MemoryAccessKindCatalog::contains(kind);
}

std::size_t memory_access_kind_to_index(const MemoryAccessKind kind) noexcept
{
    return MemoryAccessKindCatalog::index(kind);
}

std::string_view memory_access_kind_to_name(const MemoryAccessKind kind) noexcept
{
    return MemoryAccessKindCatalog::name(kind);
}

bool is_page_fault_reason_valid(const PageFaultReason reason) noexcept
{
    return PageFaultReasonCatalog::contains(reason);
}

std::size_t page_fault_reason_to_index(const PageFaultReason reason) noexcept
{
    return PageFaultReasonCatalog::index(reason);
}

std::string_view page_fault_reason_to_name(const PageFaultReason reason) noexcept
{
    return PageFaultReasonCatalog::name(reason);
}

bool is_page_table_level_valid(const PageTableLevel level) noexcept
{
    return PageTableLevelCatalog::contains(level);
}

std::size_t page_table_level_to_index(const PageTableLevel level) noexcept
{
    return PageTableLevelCatalog::index(level);
}

std::string_view page_table_level_to_name(const PageTableLevel level) noexcept
{
    return PageTableLevelCatalog::name(level);
}

std::size_t page_table_level_shift(const PageTableLevel level)
{
    switch (level)
    {
    case PageTableLevel::PT:
        return PAGE_TABLE_INDEX_SHIFT_PT;
    case PageTableLevel::PD:
        return PAGE_TABLE_INDEX_SHIFT_PD;
    case PageTableLevel::PDPT:
        return PAGE_TABLE_INDEX_SHIFT_PDPT;
    case PageTableLevel::PML4:
        return PAGE_TABLE_INDEX_SHIFT_PML4;
    case PageTableLevel::COUNT:
        break;
    }

    throw std::out_of_range{PAGING_INVALID_TABLE_LEVEL_MESSAGE};
}

Address64 page_size_for_leaf_level(const PageTableLevel level)
{
    switch (level)
    {
    case PageTableLevel::PT:
        return PAGE_SIZE_4K_BYTES;
    case PageTableLevel::PD:
        return PAGE_SIZE_2M_BYTES;
    case PageTableLevel::PDPT:
        return PAGE_SIZE_1G_BYTES;
    case PageTableLevel::PML4:
        throw std::logic_error{PAGING_PML4_LEAF_MESSAGE};
    case PageTableLevel::COUNT:
        break;
    }

    throw std::out_of_range{PAGING_INVALID_TABLE_LEVEL_MESSAGE};
}

bool can_page_table_level_be_leaf(const PageTableLevel level) noexcept
{
    return level == PageTableLevel::PT || level == PageTableLevel::PD || level == PageTableLevel::PDPT;
}

bool is_canonical_linear_address(const Address64 address) noexcept
{
    const bool sign_bit_set = (address & PAGING_CANONICAL_SIGN_BIT) != Address64{0};
    const Address64 high_bits = address & PAGING_CANONICAL_HIGH_MASK;
    return sign_bit_set ? high_bits == PAGING_CANONICAL_HIGH_MASK : high_bits == Address64{0};
}

bool is_aligned_to_page_size(const Address64 address, const Address64 page_size_bytes) noexcept
{
    return (address & (page_size_bytes - Address64{1})) == Address64{0};
}

std::size_t page_table_index(const Address64 linear_address, const PageTableLevel level)
{
    require_page_table_level(level);
    return static_cast<std::size_t>((linear_address >> page_table_level_shift(level)) & PAGE_TABLE_INDEX_MASK);
}

Address64 page_base(const Address64 linear_address, const Address64 page_size_bytes) noexcept
{
    return linear_address & ~(page_size_bytes - Address64{1});
}

Qword make_page_fault_error_code(
    const MemoryAccessKind access_kind,
    const system::PrivilegeLevel privilege_level,
    const PageFaultReason reason) noexcept
{
    Qword error_code = Qword{0};
    if (reason != PageFaultReason::NOT_PRESENT)
    {
        error_code |= PAGE_FAULT_ERROR_PRESENT_BIT;
    }
    if (access_kind == MemoryAccessKind::WRITE)
    {
        error_code |= PAGE_FAULT_ERROR_WRITE_BIT;
    }
    if (privilege_level == system::PrivilegeLevel::RING3)
    {
        error_code |= PAGE_FAULT_ERROR_USER_BIT;
    }
    if (reason == PageFaultReason::RESERVED_BIT)
    {
        error_code |= PAGE_FAULT_ERROR_RESERVED_BIT;
    }
    if (access_kind == MemoryAccessKind::EXECUTE)
    {
        error_code |= PAGE_FAULT_ERROR_INSTRUCTION_FETCH_BIT;
    }
    return error_code;
}

PagePermissions::PagePermissions(
    const bool writable,
    const bool user_accessible,
    const bool executable) noexcept :
    writable_(writable),
    user_accessible_(user_accessible), executable_(executable)
{
}

PagePermissions PagePermissions::kernel_read_only_execute() noexcept
{
    return PagePermissions{false, false, true};
}

PagePermissions PagePermissions::kernel_read_write_execute() noexcept
{
    return PagePermissions{true, false, true};
}

PagePermissions PagePermissions::kernel_read_write_no_execute() noexcept
{
    return PagePermissions{true, false, false};
}

PagePermissions PagePermissions::user_read_only_execute() noexcept
{
    return PagePermissions{false, true, true};
}

PagePermissions PagePermissions::user_read_write_execute() noexcept
{
    return PagePermissions{true, true, true};
}

PagePermissions PagePermissions::user_read_write_no_execute() noexcept
{
    return PagePermissions{true, true, false};
}

bool PagePermissions::writable() const noexcept
{
    return this->writable_;
}

bool PagePermissions::user_accessible() const noexcept
{
    return this->user_accessible_;
}

bool PagePermissions::executable() const noexcept
{
    return this->executable_;
}

PageTableEntry PageTableEntry::not_present() noexcept
{
    return PageTableEntry{Qword{0}};
}

PageTableEntry PageTableEntry::table(const Address64 table_address, const PagePermissions permissions)
{
    return PageTableEntry::present_entry(table_address, PAGE_SIZE_4K_BYTES, permissions);
}

PageTableEntry PageTableEntry::page_4k(const Address64 frame_address, const PagePermissions permissions)
{
    return PageTableEntry::present_entry(frame_address, PAGE_SIZE_4K_BYTES, permissions);
}

PageTableEntry PageTableEntry::page_2m(const Address64 frame_address, const PagePermissions permissions)
{
    PageTableEntry entry = PageTableEntry::present_entry(frame_address, PAGE_SIZE_2M_BYTES, permissions);
    entry.raw_bits_ |= PAGE_TABLE_ENTRY_HUGE_PAGE_BIT;
    return entry;
}

PageTableEntry PageTableEntry::page_1g(const Address64 frame_address, const PagePermissions permissions)
{
    PageTableEntry entry = PageTableEntry::present_entry(frame_address, PAGE_SIZE_1G_BYTES, permissions);
    entry.raw_bits_ |= PAGE_TABLE_ENTRY_HUGE_PAGE_BIT;
    return entry;
}

Qword PageTableEntry::raw_bits() const noexcept
{
    return this->raw_bits_;
}

bool PageTableEntry::is_present() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_PRESENT_BIT) != Qword{0};
}

bool PageTableEntry::is_writable() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_WRITABLE_BIT) != Qword{0};
}

bool PageTableEntry::is_user_accessible() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_USER_BIT) != Qword{0};
}

bool PageTableEntry::is_write_through() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_WRITE_THROUGH_BIT) != Qword{0};
}

bool PageTableEntry::is_cache_disabled() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_CACHE_DISABLE_BIT) != Qword{0};
}

bool PageTableEntry::is_accessed() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_ACCESSED_BIT) != Qword{0};
}

bool PageTableEntry::is_dirty() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_DIRTY_BIT) != Qword{0};
}

bool PageTableEntry::is_huge_page() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_HUGE_PAGE_BIT) != Qword{0};
}

bool PageTableEntry::is_global() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_GLOBAL_BIT) != Qword{0};
}

bool PageTableEntry::is_no_execute() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_NO_EXECUTE_BIT) != Qword{0};
}

bool PageTableEntry::has_reserved_high_bits() const noexcept
{
    return (this->raw_bits_ & PAGE_TABLE_ENTRY_RESERVED_HIGH_MASK) != Qword{0};
}

Address64 PageTableEntry::table_address() const noexcept
{
    return this->raw_bits_ & PAGE_TABLE_ENTRY_ADDRESS_MASK_4K;
}

Address64 PageTableEntry::frame_address(const Address64 page_size_bytes) const noexcept
{
    const Qword frame_mask = PAGE_TABLE_ENTRY_ADDRESS_MASK_4K & ~(page_size_bytes - Address64{1});
    return this->raw_bits_ & frame_mask;
}

PageTableEntry PageTableEntry::with_accessed() const noexcept
{
    return PageTableEntry{this->raw_bits_ | PAGE_TABLE_ENTRY_ACCESSED_BIT};
}

PageTableEntry PageTableEntry::with_dirty() const noexcept
{
    return PageTableEntry{this->raw_bits_ | PAGE_TABLE_ENTRY_ACCESSED_BIT | PAGE_TABLE_ENTRY_DIRTY_BIT};
}

PageTableEntry PageTableEntry::present_entry(
    const Address64 address,
    const Address64 alignment,
    const PagePermissions permissions)
{
    require_aligned_address(address, alignment);
    return PageTableEntry{(address & PAGE_TABLE_ENTRY_ADDRESS_MASK_4K) | permission_bits(permissions)};
}

Qword PageTableEntry::permission_bits(const PagePermissions permissions) noexcept
{
    Qword bits = PAGE_TABLE_ENTRY_PRESENT_BIT;
    if (permissions.writable())
    {
        bits |= PAGE_TABLE_ENTRY_WRITABLE_BIT;
    }
    if (permissions.user_accessible())
    {
        bits |= PAGE_TABLE_ENTRY_USER_BIT;
    }
    if (!permissions.executable())
    {
        bits |= PAGE_TABLE_ENTRY_NO_EXECUTE_BIT;
    }
    return bits;
}

bool PagingState::is_enabled() const noexcept
{
    return this->enabled_;
}

void PagingState::enable() noexcept
{
    this->enabled_ = true;
    this->bump_generation();
}

void PagingState::disable() noexcept
{
    this->enabled_ = false;
    this->bump_generation();
}

Address64 PagingState::cr3() const noexcept
{
    return this->cr3_;
}

void PagingState::load_cr3(const Address64 root_table_address)
{
    this->load_cr3(
        root_table_address,
        ProcessContextId::kernel(),
        Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
}

void PagingState::load_cr3(
    const Address64 root_table_address,
    const ProcessContextId context_id,
    const Cr3TlbFlushMode flush_mode)
{
    if (!is_aligned_to_page_size(root_table_address, PAGE_SIZE_4K_BYTES))
    {
        throw std::out_of_range{PAGING_CR3_ALIGNMENT_MESSAGE};
    }
    if (!is_process_context_id_valid(context_id))
    {
        throw std::out_of_range{PAGING_PCID_OUT_OF_RANGE_MESSAGE};
    }
    if (!this->process_context_id_enabled_ && context_id != ProcessContextId::kernel())
    {
        throw std::logic_error{PAGING_PCID_DISABLED_NON_KERNEL_MESSAGE};
    }
    if (flush_mode != Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT && flush_mode != Cr3TlbFlushMode::PRESERVE_CONTEXT)
    {
        throw std::out_of_range{PAGING_INVALID_CR3_FLUSH_MODE_MESSAGE};
    }

    this->cr3_ = root_table_address;
    this->process_context_id_ = context_id;
    if (flush_mode == Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT)
    {
        this->bump_generation();
    }
}

bool PagingState::process_context_id_enabled() const noexcept
{
    return this->process_context_id_enabled_;
}

void PagingState::set_process_context_id_enabled(const bool enabled) noexcept
{
    if (this->process_context_id_enabled_ == enabled)
    {
        return;
    }

    this->process_context_id_enabled_ = enabled;
    if (!this->process_context_id_enabled_)
    {
        this->process_context_id_ = ProcessContextId::kernel();
    }
    this->bump_generation();
}

ProcessContextId PagingState::process_context_id() const noexcept
{
    return this->process_context_id_;
}

Address64 PagingState::page_fault_linear_address() const noexcept
{
    return this->page_fault_linear_address_;
}

void PagingState::set_page_fault_linear_address(const Address64 address) noexcept
{
    this->page_fault_linear_address_ = address;
}

bool PagingState::write_protect_enabled() const noexcept
{
    return this->write_protect_enabled_;
}

void PagingState::set_write_protect_enabled(const bool enabled) noexcept
{
    this->write_protect_enabled_ = enabled;
    this->bump_generation();
}

bool PagingState::execute_disable_enabled() const noexcept
{
    return this->execute_disable_enabled_;
}

void PagingState::set_execute_disable_enabled(const bool enabled) noexcept
{
    this->execute_disable_enabled_ = enabled;
    this->bump_generation();
}

Qword PagingState::generation() const noexcept
{
    return this->generation_;
}

void PagingState::bump_generation() noexcept
{
    ++this->generation_;
}

PageTranslation::PageTranslation(
    const Address64 virtual_page_base,
    const Address64 physical_frame_base,
    const Address64 page_size_bytes,
    const PagePermissions permissions,
    const Address64 leaf_entry_address,
    const bool dirty) noexcept :
    virtual_page_base_(virtual_page_base),
    physical_frame_base_(physical_frame_base), page_size_bytes_(page_size_bytes), permissions_(permissions),
    leaf_entry_address_(leaf_entry_address), dirty_(dirty)
{
}

Address64 PageTranslation::virtual_page_base() const noexcept
{
    return this->virtual_page_base_;
}

Address64 PageTranslation::physical_frame_base() const noexcept
{
    return this->physical_frame_base_;
}

Address64 PageTranslation::page_size_bytes() const noexcept
{
    return this->page_size_bytes_;
}

const PagePermissions& PageTranslation::permissions() const noexcept
{
    return this->permissions_;
}

Address64 PageTranslation::leaf_entry_address() const noexcept
{
    return this->leaf_entry_address_;
}

bool PageTranslation::dirty() const noexcept
{
    return this->dirty_;
}

void PageTranslation::set_dirty(const bool dirty) noexcept
{
    this->dirty_ = dirty;
}

bool PageTranslation::contains(const Address64 linear_address) const noexcept
{
    return linear_address >= this->virtual_page_base_ &&
        (linear_address - this->virtual_page_base_) < this->page_size_bytes_;
}

Address64 PageTranslation::translate(const Address64 linear_address) const noexcept
{
    return this->physical_frame_base_ + (linear_address - this->virtual_page_base_);
}

PageFault::PageFault(
    const Address64 linear_address,
    const MemoryAccessKind access_kind,
    const PageFaultReason reason,
    const Qword error_code) :
    std::runtime_error(PAGING_PAGE_FAULT_MESSAGE),
    linear_address_(linear_address),
    access_kind_(access_kind),
    reason_(reason),
    error_code_(error_code)
{
    if (!is_memory_access_kind_valid(access_kind))
    {
        throw std::out_of_range{PAGING_INVALID_ACCESS_KIND_MESSAGE};
    }
    if (!is_page_fault_reason_valid(reason))
    {
        throw std::out_of_range{PAGING_INVALID_FAULT_REASON_MESSAGE};
    }
}

Address64 PageFault::linear_address() const noexcept
{
    return this->linear_address_;
}

MemoryAccessKind PageFault::access_kind() const noexcept
{
    return this->access_kind_;
}

PageFaultReason PageFault::reason() const noexcept
{
    return this->reason_;
}

Qword PageFault::error_code() const noexcept
{
    return this->error_code_;
}
}
