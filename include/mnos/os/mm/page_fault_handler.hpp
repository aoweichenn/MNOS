#pragma once

#include <cstdint>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/system/trap_frame.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>

namespace mnos::os::mm
{
enum class PageFaultResult : std::uint8_t
{
    HANDLED,
    NOT_PAGE_FAULT,
    PROTECTION_FAULT,
    OUT_OF_MEMORY,
    COUNT
};

class PageFaultHandler final
{
public:
    PageFaultHandler(PhysicalPageAllocator& allocator, AddressSpace& address_space, cpu::MemoryBus& memory_bus) noexcept;

    [[nodiscard]] PageFaultResult handle(const cpu::system::TrapFrame& trap_frame, cpu::CpuState& cpu_state);

private:
    [[nodiscard]] static bool is_not_present_fault(const cpu::system::TrapFrame& trap_frame) noexcept;
    [[nodiscard]] static cpu::memory::PagePermissions permissions_for_fault(const cpu::system::TrapFrame& trap_frame) noexcept;
    void zero_physical_page(PhysicalAddress physical_address);

    PhysicalPageAllocator* allocator_;
    AddressSpace* address_space_;
    cpu::MemoryBus* memory_bus_;
};
}
