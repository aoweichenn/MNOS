#include <mnos/os/platform/machine.hpp>

namespace mnos::os::platform
{
Machine::Machine(const std::size_t physical_memory_size_bytes) :
    physical_memory_(physical_memory_size_bytes), memory_bus_(this->physical_memory_)
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

std::size_t Machine::physical_memory_size_bytes() const noexcept
{
    return this->physical_memory_.size();
}
}
