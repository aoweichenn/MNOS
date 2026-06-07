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

bool MemoryBus::contains_range(const Address64 address, const std::size_t byte_count) const noexcept
{
    return this->physical_memory_->contains_range(address, byte_count);
}

Qword MemoryBus::read(const Address64 address, const DataSize size) const
{
    return this->physical_memory_->read(address, size);
}

void MemoryBus::write(const Address64 address, const DataSize size, const Qword value)
{
    this->physical_memory_->write(address, size, value);
}
}
