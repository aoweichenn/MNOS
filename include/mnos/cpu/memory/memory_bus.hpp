#pragma once

#include <cstddef>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>

namespace mnos::cpu
{
class MemoryBus
{
public:
    explicit MemoryBus(PhysicalMemory& physical_memory) noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool contains_range(ADDRESS64 address, std::size_t byte_count) const noexcept;
    [[nodiscard]] UQWORD64 read(ADDRESS64 address, DataSize size) const;
    void write(ADDRESS64 address, DataSize size, UQWORD64 value);

private:
    PhysicalMemory* physical_memory_;
};
}
