#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace mnos::cpu::memory
{
inline constexpr std::size_t PAGE_TABLE_LEVEL_COUNT = 4;
inline constexpr std::size_t PAGE_TABLE_ENTRY_COUNT = 512;
inline constexpr std::size_t PAGE_TABLE_ENTRY_BYTES = 8;
inline constexpr std::size_t PAGE_TABLE_INDEX_BITS = 9;
inline constexpr std::size_t PAGE_TABLE_OFFSET_BITS_4K = 12;
inline constexpr std::size_t PAGE_TABLE_OFFSET_BITS_2M = 21;
inline constexpr std::size_t PAGE_TABLE_OFFSET_BITS_1G = 30;
inline constexpr std::size_t PAGE_TABLE_INDEX_SHIFT_PT = PAGE_TABLE_OFFSET_BITS_4K;
inline constexpr std::size_t PAGE_TABLE_INDEX_SHIFT_PD = PAGE_TABLE_OFFSET_BITS_2M;
inline constexpr std::size_t PAGE_TABLE_INDEX_SHIFT_PDPT = PAGE_TABLE_OFFSET_BITS_1G;
inline constexpr std::size_t PAGE_TABLE_INDEX_SHIFT_PML4 = 39;
inline constexpr Address64 PAGE_SIZE_4K_BYTES = Address64{1} << PAGE_TABLE_OFFSET_BITS_4K;
inline constexpr Address64 PAGE_SIZE_2M_BYTES = Address64{1} << PAGE_TABLE_OFFSET_BITS_2M;
inline constexpr Address64 PAGE_SIZE_1G_BYTES = Address64{1} << PAGE_TABLE_OFFSET_BITS_1G;
inline constexpr Address64 PAGE_OFFSET_MASK_4K = PAGE_SIZE_4K_BYTES - Address64{1};
inline constexpr Qword PAGE_TABLE_INDEX_MASK = Qword{0x1FF};
inline constexpr Qword PAGE_TABLE_ENTRY_PRESENT_BIT = Qword{1} << 0;
inline constexpr Qword PAGE_TABLE_ENTRY_WRITABLE_BIT = Qword{1} << 1;
inline constexpr Qword PAGE_TABLE_ENTRY_USER_BIT = Qword{1} << 2;
inline constexpr Qword PAGE_TABLE_ENTRY_WRITE_THROUGH_BIT = Qword{1} << 3;
inline constexpr Qword PAGE_TABLE_ENTRY_CACHE_DISABLE_BIT = Qword{1} << 4;
inline constexpr Qword PAGE_TABLE_ENTRY_ACCESSED_BIT = Qword{1} << 5;
inline constexpr Qword PAGE_TABLE_ENTRY_DIRTY_BIT = Qword{1} << 6;
inline constexpr Qword PAGE_TABLE_ENTRY_HUGE_PAGE_BIT = Qword{1} << 7;
inline constexpr Qword PAGE_TABLE_ENTRY_GLOBAL_BIT = Qword{1} << 8;
inline constexpr Qword PAGE_TABLE_ENTRY_NO_EXECUTE_BIT = Qword{1} << 63;
inline constexpr Qword PAGE_TABLE_ENTRY_ADDRESS_MASK_4K = Qword{0x000F'FFFF'FFFF'F000ULL};
inline constexpr Qword PAGE_TABLE_ENTRY_RESERVED_HIGH_MASK = Qword{0x7FF0'0000'0000'0000ULL};
inline constexpr Qword PAGE_FAULT_ERROR_PRESENT_BIT = Qword{1} << 0;
inline constexpr Qword PAGE_FAULT_ERROR_WRITE_BIT = Qword{1} << 1;
inline constexpr Qword PAGE_FAULT_ERROR_USER_BIT = Qword{1} << 2;
inline constexpr Qword PAGE_FAULT_ERROR_RESERVED_BIT = Qword{1} << 3;
inline constexpr Qword PAGE_FAULT_ERROR_INSTRUCTION_FETCH_BIT = Qword{1} << 4;
inline constexpr std::uint16_t PROCESS_CONTEXT_ID_KERNEL_VALUE = std::uint16_t{0};
inline constexpr std::uint16_t PROCESS_CONTEXT_ID_BIT_COUNT = std::uint16_t{12};
inline constexpr std::uint16_t PROCESS_CONTEXT_ID_MAX_VALUE =
    static_cast<std::uint16_t>((std::uint16_t{1} << PROCESS_CONTEXT_ID_BIT_COUNT) - std::uint16_t{1});

enum class MemoryAccessKind : std::uint8_t
{
    READ,
    WRITE,
    EXECUTE,
    COUNT,
};

inline constexpr std::size_t MEMORY_ACCESS_KIND_COUNT = static_cast<std::size_t>(MemoryAccessKind::COUNT);

enum class PageFaultReason : std::uint8_t
{
    NOT_PRESENT,
    WRITE_PROTECTION,
    USER_SUPERVISOR,
    EXECUTE_DISABLE,
    RESERVED_BIT,
    COUNT,
};

inline constexpr std::size_t PAGE_FAULT_REASON_COUNT = static_cast<std::size_t>(PageFaultReason::COUNT);

enum class PageTableLevel : std::uint8_t
{
    PT,
    PD,
    PDPT,
    PML4,
    COUNT,
};

inline constexpr std::size_t PAGE_TABLE_LEVEL_ENUM_COUNT = static_cast<std::size_t>(PageTableLevel::COUNT);

enum class Cr3TlbFlushMode : std::uint8_t
{
    FLUSH_CURRENT_CONTEXT,
    PRESERVE_CONTEXT,
};

class ProcessContextId final
{
public:
    using value_type = std::uint16_t;

    constexpr ProcessContextId() noexcept = default;
    explicit constexpr ProcessContextId(value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] static constexpr ProcessContextId kernel() noexcept
    {
        return ProcessContextId{PROCESS_CONTEXT_ID_KERNEL_VALUE};
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] constexpr bool operator==(const ProcessContextId& other) const noexcept = default;

private:
    value_type value_ = PROCESS_CONTEXT_ID_KERNEL_VALUE;
};

[[nodiscard]] bool is_process_context_id_valid(ProcessContextId context_id) noexcept;
[[nodiscard]] bool is_memory_access_kind_valid(MemoryAccessKind kind) noexcept;
[[nodiscard]] std::size_t memory_access_kind_to_index(MemoryAccessKind kind) noexcept;
[[nodiscard]] std::string_view memory_access_kind_to_name(MemoryAccessKind kind) noexcept;

[[nodiscard]] bool is_page_fault_reason_valid(PageFaultReason reason) noexcept;
[[nodiscard]] std::size_t page_fault_reason_to_index(PageFaultReason reason) noexcept;
[[nodiscard]] std::string_view page_fault_reason_to_name(PageFaultReason reason) noexcept;

[[nodiscard]] bool is_page_table_level_valid(PageTableLevel level) noexcept;
[[nodiscard]] std::size_t page_table_level_to_index(PageTableLevel level) noexcept;
[[nodiscard]] std::string_view page_table_level_to_name(PageTableLevel level) noexcept;
[[nodiscard]] std::size_t page_table_level_shift(PageTableLevel level);
[[nodiscard]] Address64 page_size_for_leaf_level(PageTableLevel level);
[[nodiscard]] bool can_page_table_level_be_leaf(PageTableLevel level) noexcept;

[[nodiscard]] bool is_canonical_linear_address(Address64 address) noexcept;
[[nodiscard]] bool is_aligned_to_page_size(Address64 address, Address64 page_size_bytes) noexcept;
[[nodiscard]] std::size_t page_table_index(Address64 linear_address, PageTableLevel level);
[[nodiscard]] Address64 page_base(Address64 linear_address, Address64 page_size_bytes) noexcept;
[[nodiscard]] Qword make_page_fault_error_code(
    MemoryAccessKind access_kind,
    system::PrivilegeLevel privilege_level,
    PageFaultReason reason) noexcept;

class PagePermissions final
{
public:
    PagePermissions() noexcept = default;
    PagePermissions(bool writable, bool user_accessible, bool executable) noexcept;

    [[nodiscard]] static PagePermissions kernel_read_only_execute() noexcept;
    [[nodiscard]] static PagePermissions kernel_read_write_execute() noexcept;
    [[nodiscard]] static PagePermissions kernel_read_write_no_execute() noexcept;
    [[nodiscard]] static PagePermissions user_read_only_execute() noexcept;
    [[nodiscard]] static PagePermissions user_read_write_execute() noexcept;
    [[nodiscard]] static PagePermissions user_read_write_no_execute() noexcept;

    [[nodiscard]] bool writable() const noexcept;
    [[nodiscard]] bool user_accessible() const noexcept;
    [[nodiscard]] bool executable() const noexcept;

private:
    bool writable_ = false;
    bool user_accessible_ = false;
    bool executable_ = true;
};

class PageTableEntry final
{
public:
    constexpr PageTableEntry() noexcept = default;
    explicit constexpr PageTableEntry(Qword raw_bits) noexcept : raw_bits_(raw_bits)
    {
    }

    [[nodiscard]] static PageTableEntry not_present() noexcept;
    [[nodiscard]] static PageTableEntry table(Address64 table_address, PagePermissions permissions);
    [[nodiscard]] static PageTableEntry page_4k(Address64 frame_address, PagePermissions permissions);
    [[nodiscard]] static PageTableEntry page_2m(Address64 frame_address, PagePermissions permissions);
    [[nodiscard]] static PageTableEntry page_1g(Address64 frame_address, PagePermissions permissions);

    [[nodiscard]] Qword raw_bits() const noexcept;
    [[nodiscard]] bool is_present() const noexcept;
    [[nodiscard]] bool is_writable() const noexcept;
    [[nodiscard]] bool is_user_accessible() const noexcept;
    [[nodiscard]] bool is_write_through() const noexcept;
    [[nodiscard]] bool is_cache_disabled() const noexcept;
    [[nodiscard]] bool is_accessed() const noexcept;
    [[nodiscard]] bool is_dirty() const noexcept;
    [[nodiscard]] bool is_huge_page() const noexcept;
    [[nodiscard]] bool is_global() const noexcept;
    [[nodiscard]] bool is_no_execute() const noexcept;
    [[nodiscard]] bool has_reserved_high_bits() const noexcept;
    [[nodiscard]] Address64 table_address() const noexcept;
    [[nodiscard]] Address64 frame_address(Address64 page_size_bytes) const noexcept;

    [[nodiscard]] PageTableEntry with_accessed() const noexcept;
    [[nodiscard]] PageTableEntry with_dirty() const noexcept;
    [[nodiscard]] PageTableEntry with_permissions(PagePermissions permissions) const noexcept;

private:
    [[nodiscard]] static PageTableEntry present_entry(Address64 address, Address64 alignment, PagePermissions permissions);
    [[nodiscard]] static Qword permission_bits(PagePermissions permissions) noexcept;

    Qword raw_bits_ = Qword{0};
};

class PagingState final
{
public:
    [[nodiscard]] bool is_enabled() const noexcept;
    void enable() noexcept;
    void disable() noexcept;

    [[nodiscard]] Address64 cr3() const noexcept;
    void load_cr3(Address64 root_table_address);
    void load_cr3(Address64 root_table_address, ProcessContextId context_id, Cr3TlbFlushMode flush_mode);

    [[nodiscard]] bool process_context_id_enabled() const noexcept;
    void set_process_context_id_enabled(bool enabled) noexcept;
    [[nodiscard]] ProcessContextId process_context_id() const noexcept;

    [[nodiscard]] Address64 page_fault_linear_address() const noexcept;
    void set_page_fault_linear_address(Address64 address) noexcept;

    [[nodiscard]] bool write_protect_enabled() const noexcept;
    void set_write_protect_enabled(bool enabled) noexcept;

    [[nodiscard]] bool execute_disable_enabled() const noexcept;
    void set_execute_disable_enabled(bool enabled) noexcept;

    [[nodiscard]] Qword generation() const noexcept;

private:
    void bump_generation() noexcept;

    Address64 cr3_ = Address64{0};
    Address64 page_fault_linear_address_ = Address64{0};
    Qword generation_ = Qword{0};
    ProcessContextId process_context_id_;
    bool enabled_ = false;
    bool process_context_id_enabled_ = false;
    bool write_protect_enabled_ = true;
    bool execute_disable_enabled_ = true;
};

class PageTranslation final
{
public:
    PageTranslation() noexcept = default;
    PageTranslation(
        Address64 virtual_page_base,
        Address64 physical_frame_base,
        Address64 page_size_bytes,
        PagePermissions permissions,
        Address64 leaf_entry_address,
        bool dirty) noexcept;

    [[nodiscard]] Address64 virtual_page_base() const noexcept;
    [[nodiscard]] Address64 physical_frame_base() const noexcept;
    [[nodiscard]] Address64 page_size_bytes() const noexcept;
    [[nodiscard]] const PagePermissions& permissions() const noexcept;
    [[nodiscard]] Address64 leaf_entry_address() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    void set_dirty(bool dirty) noexcept;
    [[nodiscard]] bool contains(Address64 linear_address) const noexcept;
    [[nodiscard]] Address64 translate(Address64 linear_address) const noexcept;

private:
    Address64 virtual_page_base_ = Address64{0};
    Address64 physical_frame_base_ = Address64{0};
    Address64 page_size_bytes_ = PAGE_SIZE_4K_BYTES;
    PagePermissions permissions_;
    Address64 leaf_entry_address_ = Address64{0};
    bool dirty_ = false;
};

class PageFault final : public std::runtime_error
{
public:
    PageFault(
        Address64 linear_address,
        MemoryAccessKind access_kind,
        PageFaultReason reason,
        Qword error_code);

    [[nodiscard]] Address64 linear_address() const noexcept;
    [[nodiscard]] MemoryAccessKind access_kind() const noexcept;
    [[nodiscard]] PageFaultReason reason() const noexcept;
    [[nodiscard]] Qword error_code() const noexcept;

private:
    Address64 linear_address_;
    MemoryAccessKind access_kind_;
    PageFaultReason reason_;
    Qword error_code_;
};
}
