#include <new>
#include <stdexcept>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/os/mm/page_fault_handler.hpp>

namespace
{
[[nodiscard]] bool has_page_fault_error_bit(
    const mnos::cpu::system::TrapFrame& trap_frame,
    const mnos::cpu::Qword bit) noexcept
{
    return trap_frame.has_error_code() && ((trap_frame.error_code() & bit) != mnos::cpu::Qword{0});
}
}

namespace mnos::os::mm
{
PageFaultHandler::PageFaultHandler(
    PhysicalPageAllocator& allocator,
    AddressSpace& address_space,
    cpu::MemoryBus& memory_bus) noexcept :
    allocator_(&allocator),
    address_space_(&address_space),
    memory_bus_(&memory_bus)
{
}

PageFaultResult PageFaultHandler::handle(
    const cpu::system::TrapFrame& trap_frame,
    cpu::CpuState& cpu_state)
{
    if (trap_frame.vector() != cpu::system::InterruptVector::page_fault())
    {
        return PageFaultResult::NOT_PAGE_FAULT;
    }

    if (!PageFaultHandler::is_not_present_fault(trap_frame))
    {
        return PageFaultResult::PROTECTION_FAULT;
    }

    PhysicalAddress physical_page;
    try
    {
        physical_page = this->allocator_->allocate_page();
    }
    catch (const std::bad_alloc&)
    {
        return PageFaultResult::OUT_OF_MEMORY;
    }

    try
    {
        this->zero_physical_page(physical_page);
        this->address_space_->map_page(
            align_down(VirtualAddress{cpu_state.paging().page_fault_linear_address()}),
            physical_page,
            PageFaultHandler::permissions_for_fault(trap_frame));
    }
    catch (const std::bad_alloc&)
    {
        this->allocator_->free_page(physical_page);
        return PageFaultResult::OUT_OF_MEMORY;
    }
    catch (const std::out_of_range&)
    {
        this->allocator_->free_page(physical_page);
        return PageFaultResult::OUT_OF_MEMORY;
    }

    this->address_space_->activate(cpu_state);
    cpu_state.clear_pending_trap();
    return PageFaultResult::HANDLED;
}

bool PageFaultHandler::is_not_present_fault(const cpu::system::TrapFrame& trap_frame) noexcept
{
    return trap_frame.has_error_code() &&
        !has_page_fault_error_bit(trap_frame, cpu::memory::PAGE_FAULT_ERROR_PRESENT_BIT) &&
        !has_page_fault_error_bit(trap_frame, cpu::memory::PAGE_FAULT_ERROR_RESERVED_BIT);
}

cpu::memory::PagePermissions PageFaultHandler::permissions_for_fault(
    const cpu::system::TrapFrame& trap_frame) noexcept
{
    const bool user_fault = has_page_fault_error_bit(trap_frame, cpu::memory::PAGE_FAULT_ERROR_USER_BIT);
    const bool instruction_fetch_fault =
        has_page_fault_error_bit(trap_frame, cpu::memory::PAGE_FAULT_ERROR_INSTRUCTION_FETCH_BIT);

    if (user_fault && instruction_fetch_fault)
    {
        return cpu::memory::PagePermissions::user_read_only_execute();
    }
    if (user_fault)
    {
        return cpu::memory::PagePermissions::user_read_write_no_execute();
    }
    if (instruction_fetch_fault)
    {
        return cpu::memory::PagePermissions::kernel_read_only_execute();
    }
    return cpu::memory::PagePermissions::kernel_read_write_no_execute();
}

void PageFaultHandler::zero_physical_page(const PhysicalAddress physical_address)
{
    for (AddressValue offset = AddressValue{0}; offset < MM_PAGE_SIZE_BYTES; offset += cpu::DATA_SIZE_QWORD_BYTES)
    {
        this->memory_bus_->write(
            to_cpu_address(physical_address + offset),
            cpu::DataSize::QWORD,
            cpu::Qword{0});
    }
}
}
