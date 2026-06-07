#include <mnos/cpu/memory/memory_bus.hpp>

namespace mnos::cpu
{
MemoryBus::MemoryBus(PhysicalMemory& physical_memory) noexcept : physical_memory_(&physical_memory)
{
}

bool MemoryBus::empty() const noexcept
{
    return this->physical_memory_->empty();
}

std::size_t MemoryBus::size() const noexcept
{
    return this->physical_memory_->size();
}

bool MemoryBus::contains_range(const ADDRESS64 address, const std::size_t byte_count) const noexcept
{
    return this->physical_memory_->contains_range(address, byte_count);
}

UQWORD64 MemoryBus::read(const ADDRESS64 address, const DataSize size) const
{
    return this->physical_memory_->read(address, size);
}

void MemoryBus::write(const ADDRESS64 address, const DataSize size, const UQWORD64 value)
{
    this->physical_memory_->write(address, size, value);
}
}
