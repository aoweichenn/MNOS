#include <algorithm>
#include <array>

#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/perf/performance_model.hpp>

namespace
{
constexpr mnos::cpu::Qword MMU_BYTE_MASK = mnos::cpu::Qword{0xFF};

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

[[nodiscard]] bool is_access_within_4k_page(
    const mnos::cpu::Address64 linear_address,
    const std::size_t byte_count) noexcept
{
    const mnos::cpu::Address64 offset = linear_address & mnos::cpu::memory::PAGE_OFFSET_MASK_4K;
    return offset + static_cast<mnos::cpu::Address64>(byte_count) <= mnos::cpu::memory::PAGE_SIZE_4K_BYTES;
}
}

namespace mnos::cpu::memory
{
TranslationLookasideBuffer& MemoryManagementUnit::tlb() noexcept
{
    return this->tlb_;
}

const TranslationLookasideBuffer& MemoryManagementUnit::tlb() const noexcept
{
    return this->tlb_;
}

void MemoryManagementUnit::attach_stage8_performance_model(
    mnos::cpu::perf::Stage8PerformanceModel& performance_model) noexcept
{
    this->stage8_performance_model_ = &performance_model;
}

void MemoryManagementUnit::detach_stage8_performance_model() noexcept
{
    this->stage8_performance_model_ = nullptr;
}

bool MemoryManagementUnit::has_stage8_performance_model() const noexcept
{
    return this->stage8_performance_model_ != nullptr;
}

void MemoryManagementUnit::flush_tlb() noexcept
{
    this->tlb_.clear();
}

void MemoryManagementUnit::flush_tlb_context(const ProcessContextId context_id) noexcept
{
    this->tlb_.invalidate_context(context_id);
}

void MemoryManagementUnit::invalidate_page(const Address64 linear_address) noexcept
{
    this->tlb_.invalidate_page(linear_address);
}

void MemoryManagementUnit::invalidate_page(
    const Address64 linear_address,
    const ProcessContextId context_id) noexcept
{
    this->tlb_.invalidate_page(linear_address, context_id);
}

Address64 MemoryManagementUnit::translate(
    MemoryBus& memory_bus,
    PagingState& paging_state,
    const system::PrivilegeLevel privilege_level,
    const Address64 linear_address,
    const MemoryAccessKind access_kind)
{
    if (!paging_state.is_enabled())
    {
        return linear_address;
    }

    return this->translate_enabled(memory_bus, paging_state, privilege_level, linear_address, access_kind);
}

Qword MemoryManagementUnit::read(
    MemoryBus& memory_bus,
    PagingState& paging_state,
    const system::PrivilegeLevel privilege_level,
    const Address64 linear_address,
    const DataSize data_size)
{
    const std::size_t byte_count = data_size_to_bytes(data_size);
    if (!paging_state.is_enabled() || is_access_within_4k_page(linear_address, byte_count))
    {
        const Address64 physical_address =
            this->translate(memory_bus, paging_state, privilege_level, linear_address, MemoryAccessKind::READ);
        this->record_data_read(physical_address, byte_count);
        return memory_bus.read(physical_address, data_size);
    }

    Qword value = Qword{0};
    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const Address64 byte_linear_address = linear_address + static_cast<Address64>(byte_index);
        const Address64 byte_physical_address =
            this->translate_enabled(memory_bus, paging_state, privilege_level, byte_linear_address, MemoryAccessKind::READ);
        this->record_data_read(byte_physical_address, DATA_SIZE_BYTE_BYTES);
        value |= memory_bus.read(byte_physical_address, DataSize::BYTE) << (byte_index * DATA_SIZE_BYTE_BITS);
    }
    return value;
}

void MemoryManagementUnit::write(
    MemoryBus& memory_bus,
    PagingState& paging_state,
    const system::PrivilegeLevel privilege_level,
    const Address64 linear_address,
    const DataSize data_size,
    const Qword value)
{
    const std::size_t byte_count = data_size_to_bytes(data_size);
    if (!paging_state.is_enabled() || is_access_within_4k_page(linear_address, byte_count))
    {
        const Address64 physical_address =
            this->translate(memory_bus, paging_state, privilege_level, linear_address, MemoryAccessKind::WRITE);
        this->record_data_write(physical_address, byte_count);
        memory_bus.write(physical_address, data_size, value);
        return;
    }

    std::array<Address64, DATA_SIZE_QWORD_BYTES> physical_addresses{};
    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        physical_addresses[byte_index] = this->translate_enabled(
            memory_bus,
            paging_state,
            privilege_level,
            linear_address + static_cast<Address64>(byte_index),
            MemoryAccessKind::WRITE);
    }

    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const Byte byte_value = static_cast<Byte>((value >> (byte_index * DATA_SIZE_BYTE_BITS)) & MMU_BYTE_MASK);
        this->record_data_write(physical_addresses[byte_index], DATA_SIZE_BYTE_BYTES);
        memory_bus.write(physical_addresses[byte_index], DataSize::BYTE, byte_value);
    }
}

void MemoryManagementUnit::check_access_range(
    MemoryBus& memory_bus,
    PagingState& paging_state,
    const system::PrivilegeLevel privilege_level,
    Address64 linear_address,
    std::size_t byte_count,
    const MemoryAccessKind access_kind)
{
    if (!paging_state.is_enabled() || byte_count == std::size_t{0})
    {
        return;
    }

    while (byte_count > std::size_t{0})
    {
        static_cast<void>(
            this->translate_enabled(memory_bus, paging_state, privilege_level, linear_address, access_kind));
        const Address64 offset = linear_address & PAGE_OFFSET_MASK_4K;
        const std::size_t bytes_until_next_page =
            static_cast<std::size_t>(PAGE_SIZE_4K_BYTES - offset);
        const std::size_t step = std::min(byte_count, bytes_until_next_page);
        linear_address += static_cast<Address64>(step);
        byte_count -= step;
    }
}

Address64 MemoryManagementUnit::translate_enabled(
    MemoryBus& memory_bus,
    PagingState& paging_state,
    const system::PrivilegeLevel privilege_level,
    const Address64 linear_address,
    const MemoryAccessKind access_kind)
{
    if (PageTranslation* const cached_translation =
            this->tlb_.lookup(linear_address, paging_state.generation(), paging_state.process_context_id()))
    {
        this->record_tlb_hit();
        this->ensure_cached_access_allowed(
            paging_state,
            *cached_translation,
            privilege_level,
            linear_address,
            access_kind);
        if (access_kind == MemoryAccessKind::WRITE && !cached_translation->dirty())
        {
            this->mark_cached_translation_dirty(memory_bus, *cached_translation);
        }
        return cached_translation->translate(linear_address);
    }

    this->record_tlb_miss();
    PageTranslation translation = this->page_table_walker_.translate(
        memory_bus,
        paging_state,
        linear_address,
        access_kind,
        privilege_level);
    const Address64 physical_address = translation.translate(linear_address);
    this->tlb_.insert(translation, paging_state.generation(), paging_state.process_context_id());
    return physical_address;
}

void MemoryManagementUnit::ensure_cached_access_allowed(
    const PagingState& paging_state,
    const PageTranslation& translation,
    const system::PrivilegeLevel privilege_level,
    const Address64 linear_address,
    const MemoryAccessKind access_kind) const
{
    const PagePermissions& permissions = translation.permissions();
    if (is_user_access(privilege_level) && !permissions.user_accessible())
    {
        throw_page_fault(linear_address, access_kind, privilege_level, PageFaultReason::USER_SUPERVISOR);
    }

    if (access_kind == MemoryAccessKind::WRITE && !permissions.writable() &&
        !is_write_allowed_for_read_only_page(paging_state, privilege_level))
    {
        throw_page_fault(linear_address, access_kind, privilege_level, PageFaultReason::WRITE_PROTECTION);
    }

    if (access_kind == MemoryAccessKind::EXECUTE && !permissions.executable())
    {
        throw_page_fault(linear_address, access_kind, privilege_level, PageFaultReason::EXECUTE_DISABLE);
    }
}

void MemoryManagementUnit::mark_cached_translation_dirty(
    MemoryBus& memory_bus,
    PageTranslation& translation) const
{
    const PageTableEntry entry{memory_bus.read(translation.leaf_entry_address(), DataSize::QWORD)};
    const PageTableEntry updated_entry = entry.with_dirty();
    if (updated_entry.raw_bits() != entry.raw_bits())
    {
        memory_bus.write(translation.leaf_entry_address(), DataSize::QWORD, updated_entry.raw_bits());
    }
    translation.set_dirty(true);
}

void MemoryManagementUnit::record_data_read(
    const Address64 physical_address,
    const std::size_t byte_count)
{
    if (this->stage8_performance_model_ != nullptr)
    {
        this->stage8_performance_model_->record_data_read(physical_address, byte_count);
    }
}

void MemoryManagementUnit::record_data_write(
    const Address64 physical_address,
    const std::size_t byte_count)
{
    if (this->stage8_performance_model_ != nullptr)
    {
        this->stage8_performance_model_->record_data_write(physical_address, byte_count);
    }
}

void MemoryManagementUnit::record_tlb_hit() noexcept
{
    if (this->stage8_performance_model_ != nullptr)
    {
        this->stage8_performance_model_->record_tlb_hit();
    }
}

void MemoryManagementUnit::record_tlb_miss() noexcept
{
    if (this->stage8_performance_model_ != nullptr)
    {
        this->stage8_performance_model_->record_tlb_miss();
    }
}
}
