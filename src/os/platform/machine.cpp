#include <mnos/os/platform/machine.hpp>

namespace mnos::os::platform
{
Machine::Machine(const std::size_t physical_memory_size_bytes) :
    Machine(physical_memory_size_bytes, cpu::system::CORE_TOPOLOGY_DEFAULT_CORE_COUNT)
{
}

Machine::Machine(const std::size_t physical_memory_size_bytes, const std::uint32_t core_count) :
    physical_memory_(physical_memory_size_bytes),
    memory_bus_(this->physical_memory_), core_topology_(core_count)
{
}

cpu::PhysicalMemory& Machine::physical_memory() noexcept
{
    return this->physical_memory_;
}

const cpu::PhysicalMemory& Machine::physical_memory() const noexcept
{
    return this->physical_memory_;
}

cpu::MemoryBus& Machine::memory_bus() noexcept
{
    return this->memory_bus_;
}

const cpu::MemoryBus& Machine::memory_bus() const noexcept
{
    return this->memory_bus_;
}

cpu::system::CoreTopology& Machine::core_topology() noexcept
{
    return this->core_topology_;
}

const cpu::system::CoreTopology& Machine::core_topology() const noexcept
{
    return this->core_topology_;
}

std::uint32_t Machine::processor_count() const noexcept
{
    return this->core_topology_.core_count();
}

dev::TerminalDevice& Machine::terminal_device() noexcept
{
    return this->terminal_device_;
}

const dev::TerminalDevice& Machine::terminal_device() const noexcept
{
    return this->terminal_device_;
}

std::size_t Machine::physical_memory_size_bytes() const noexcept
{
    return this->physical_memory_.size();
}
}
