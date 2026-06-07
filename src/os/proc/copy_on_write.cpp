#include <new>
#include <stdexcept>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/proc/copy_on_write.hpp>

namespace
{
constexpr const char* COW_UNALIGNED_PAGE_MESSAGE = "copy-on-write virtual page must be page aligned";
constexpr const char* COW_LARGE_PAGE_MESSAGE = "copy-on-write only supports 4KiB pages";
constexpr const char* COW_DUPLICATE_MAPPING_MESSAGE = "copy-on-write mapping already exists";

[[nodiscard]] bool has_fault_bit(
    const mnos::cpu::system::TrapFrame& trap_frame,
    const mnos::cpu::Qword bit) noexcept
{
    return trap_frame.has_error_code() && ((trap_frame.error_code() & bit) != mnos::cpu::Qword{0});
}

[[nodiscard]] bool is_write_protection_fault(
    const mnos::cpu::system::TrapFrame& trap_frame) noexcept
{
    return trap_frame.vector() == mnos::cpu::system::InterruptVector::page_fault() &&
        has_fault_bit(trap_frame, mnos::cpu::memory::PAGE_FAULT_ERROR_PRESENT_BIT) &&
        has_fault_bit(trap_frame, mnos::cpu::memory::PAGE_FAULT_ERROR_WRITE_BIT);
}

[[nodiscard]] mnos::cpu::memory::PagePermissions read_only_permissions(
    const mnos::cpu::memory::PagePermissions permissions) noexcept
{
    return mnos::cpu::memory::PagePermissions{false, permissions.user_accessible(), permissions.executable()};
}

[[nodiscard]] mnos::os::mm::PhysicalAddress physical_frame_from(
    const mnos::cpu::memory::PageTranslation& translation) noexcept
{
    return mnos::os::mm::PhysicalAddress{
        static_cast<mnos::os::mm::AddressValue>(translation.physical_frame_base())};
}
}

namespace mnos::os::proc
{
CopyOnWriteManager::CowMapping::CowMapping(
    const ProcessId process_id,
    const mm::VirtualAddress virtual_page,
    const mm::PhysicalAddress physical_frame,
    const cpu::memory::PagePermissions restored_permissions) noexcept :
    process_id_(process_id),
    virtual_page_(virtual_page),
    physical_frame_(physical_frame),
    restored_permissions_(restored_permissions)
{
}

ProcessId CopyOnWriteManager::CowMapping::process_id() const noexcept
{
    return this->process_id_;
}

mm::VirtualAddress CopyOnWriteManager::CowMapping::virtual_page() const noexcept
{
    return this->virtual_page_;
}

mm::PhysicalAddress CopyOnWriteManager::CowMapping::physical_frame() const noexcept
{
    return this->physical_frame_;
}

cpu::memory::PagePermissions CopyOnWriteManager::CowMapping::restored_permissions() const noexcept
{
    return this->restored_permissions_;
}

CopyOnWriteManager::CowFrameRef::CowFrameRef(const mm::PhysicalAddress physical_frame) noexcept :
    physical_frame_(physical_frame)
{
}

mm::PhysicalAddress CopyOnWriteManager::CowFrameRef::physical_frame() const noexcept
{
    return this->physical_frame_;
}

std::size_t CopyOnWriteManager::CowFrameRef::count() const noexcept
{
    return this->count_;
}

void CopyOnWriteManager::CowFrameRef::increment() noexcept
{
    ++this->count_;
}

void CopyOnWriteManager::CowFrameRef::decrement() noexcept
{
    --this->count_;
}

std::size_t CopyOnWriteManager::mapping_count() const noexcept
{
    return this->mappings_.size();
}

std::size_t CopyOnWriteManager::frame_ref_count(const mm::PhysicalAddress physical_frame) const noexcept
{
    const CowFrameRef* const frame_ref = this->find_frame_ref(physical_frame);
    return frame_ref == nullptr ? std::size_t{0} : frame_ref->count();
}

bool CopyOnWriteManager::contains(
    const ProcessId process_id,
    const mm::VirtualAddress virtual_page) const noexcept
{
    return this->find_mapping(process_id, virtual_page) != nullptr;
}

std::size_t CopyOnWriteManager::share_pages(
    Process& parent_process,
    Process& child_process,
    std::span<const mm::VirtualAddress> virtual_pages)
{
    std::size_t shared_page_count = std::size_t{0};
    for (const mm::VirtualAddress requested_page : virtual_pages)
    {
        if (!mm::is_page_aligned(requested_page))
        {
            throw std::invalid_argument{COW_UNALIGNED_PAGE_MESSAGE};
        }

        const cpu::memory::PageTranslation translation =
            parent_process.address_space().page_translation(requested_page);
        if (translation.page_size_bytes() != cpu::memory::PAGE_SIZE_4K_BYTES)
        {
            throw std::logic_error{COW_LARGE_PAGE_MESSAGE};
        }

        const mm::PhysicalAddress physical_frame = physical_frame_from(translation);
        const CowMapping* const parent_mapping = this->find_mapping(parent_process.id(), requested_page);
        const cpu::memory::PagePermissions restored_permissions =
            parent_mapping == nullptr ? translation.permissions() : parent_mapping->restored_permissions();
        const cpu::memory::PagePermissions child_permissions = restored_permissions.writable()
            ? read_only_permissions(restored_permissions)
            : restored_permissions;

        if (restored_permissions.writable())
        {
            if (this->contains(child_process.id(), requested_page))
            {
                throw std::logic_error{COW_DUPLICATE_MAPPING_MESSAGE};
            }
        }

        child_process.address_space().map_page(requested_page, physical_frame, child_permissions);

        if (restored_permissions.writable())
        {
            if (parent_mapping == nullptr)
            {
                parent_process.address_space().set_page_permissions(
                    requested_page,
                    read_only_permissions(restored_permissions));
                this->mappings_.emplace_back(parent_process.id(), requested_page, physical_frame, restored_permissions);
                this->add_frame_ref(physical_frame);
            }
            this->mappings_.emplace_back(child_process.id(), requested_page, physical_frame, restored_permissions);
            this->add_frame_ref(physical_frame);
            ++shared_page_count;
        }
    }
    return shared_page_count;
}

CowFaultResult CopyOnWriteManager::resolve_write_fault(
    mm::PhysicalPageAllocator& allocator,
    cpu::MemoryBus& memory_bus,
    Process& process,
    cpu::CpuState& cpu_state)
{
    if (!cpu_state.has_pending_trap() || !is_write_protection_fault(cpu_state.pending_trap()))
    {
        return CowFaultResult::INVALID_FAULT;
    }

    const mm::VirtualAddress virtual_page =
        mm::align_down(mm::VirtualAddress{static_cast<mm::AddressValue>(cpu_state.paging().page_fault_linear_address())});
    CowMapping* const mapping = this->find_mapping(process.id(), virtual_page);
    if (mapping == nullptr)
    {
        return CowFaultResult::NOT_COW;
    }

    const mm::PhysicalAddress original_frame = mapping->physical_frame();
    const cpu::memory::PagePermissions restored_permissions = mapping->restored_permissions();
    if (this->frame_ref_count(original_frame) <= std::size_t{1})
    {
        process.address_space().set_page_permissions(virtual_page, restored_permissions);
        process.address_space().activate(
            cpu_state,
            cpu_state.paging().process_context_id(),
            cpu::memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
        this->release_frame_ref(original_frame);
        this->remove_mapping(process.id(), virtual_page);
        cpu_state.clear_pending_trap();
        return CowFaultResult::RESTORED;
    }

    mm::PhysicalAddress copied_page;
    try
    {
        copied_page = allocator.allocate_page();
    }
    catch (const std::bad_alloc&)
    {
        return CowFaultResult::OUT_OF_MEMORY;
    }

    try
    {
        this->copy_physical_page(memory_bus, original_frame, copied_page);
        process.address_space().map_page(virtual_page, copied_page, restored_permissions);
        process.address_space().activate(
            cpu_state,
            cpu_state.paging().process_context_id(),
            cpu::memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    }
    catch (...)
    {
        allocator.free_page(copied_page);
        throw;
    }

    this->release_frame_ref(original_frame);
    this->remove_mapping(process.id(), virtual_page);
    cpu_state.clear_pending_trap();
    return CowFaultResult::COPIED;
}

CopyOnWriteManager::CowMapping* CopyOnWriteManager::find_mapping(
    const ProcessId process_id,
    const mm::VirtualAddress virtual_page) noexcept
{
    for (CowMapping& mapping : this->mappings_)
    {
        if (mapping.process_id() == process_id && mapping.virtual_page() == virtual_page)
        {
            return &mapping;
        }
    }
    return nullptr;
}

const CopyOnWriteManager::CowMapping* CopyOnWriteManager::find_mapping(
    const ProcessId process_id,
    const mm::VirtualAddress virtual_page) const noexcept
{
    for (const CowMapping& mapping : this->mappings_)
    {
        if (mapping.process_id() == process_id && mapping.virtual_page() == virtual_page)
        {
            return &mapping;
        }
    }
    return nullptr;
}

CopyOnWriteManager::CowFrameRef* CopyOnWriteManager::find_frame_ref(
    const mm::PhysicalAddress physical_frame) noexcept
{
    for (CowFrameRef& frame_ref : this->frame_refs_)
    {
        if (frame_ref.physical_frame() == physical_frame)
        {
            return &frame_ref;
        }
    }
    return nullptr;
}

const CopyOnWriteManager::CowFrameRef* CopyOnWriteManager::find_frame_ref(
    const mm::PhysicalAddress physical_frame) const noexcept
{
    for (const CowFrameRef& frame_ref : this->frame_refs_)
    {
        if (frame_ref.physical_frame() == physical_frame)
        {
            return &frame_ref;
        }
    }
    return nullptr;
}

void CopyOnWriteManager::add_frame_ref(const mm::PhysicalAddress physical_frame)
{
    CowFrameRef* const frame_ref = this->find_frame_ref(physical_frame);
    if (frame_ref == nullptr)
    {
        this->frame_refs_.emplace_back(physical_frame);
        return;
    }
    frame_ref->increment();
}

void CopyOnWriteManager::release_frame_ref(const mm::PhysicalAddress physical_frame) noexcept
{
    for (auto frame_ref = this->frame_refs_.begin(); frame_ref != this->frame_refs_.end(); ++frame_ref)
    {
        if (frame_ref->physical_frame() == physical_frame)
        {
            frame_ref->decrement();
            if (frame_ref->count() == std::size_t{0})
            {
                static_cast<void>(this->frame_refs_.erase(frame_ref));
            }
            return;
        }
    }
}

void CopyOnWriteManager::remove_mapping(
    const ProcessId process_id,
    const mm::VirtualAddress virtual_page) noexcept
{
    for (auto mapping = this->mappings_.begin(); mapping != this->mappings_.end(); ++mapping)
    {
        if (mapping->process_id() == process_id && mapping->virtual_page() == virtual_page)
        {
            static_cast<void>(this->mappings_.erase(mapping));
            return;
        }
    }
}

void CopyOnWriteManager::copy_physical_page(
    cpu::MemoryBus& memory_bus,
    const mm::PhysicalAddress source_frame,
    const mm::PhysicalAddress target_frame)
{
    for (mm::AddressValue offset = mm::AddressValue{0}; offset < mm::MM_PAGE_SIZE_BYTES; offset += cpu::DATA_SIZE_QWORD_BYTES)
    {
        const cpu::Qword value = memory_bus.read(mm::to_cpu_address(source_frame + offset), cpu::DataSize::QWORD);
        memory_bus.write(mm::to_cpu_address(target_frame + offset), cpu::DataSize::QWORD, value);
    }
}
}
