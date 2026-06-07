#pragma once

#include <cstddef>

#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>

namespace mnos::os::platform
{
class Machine final
{
public:
    explicit Machine(std::size_t physical_memory_size_bytes);

    [[nodiscard]] cpu::PhysicalMemory& physical_memory() noexcept;
    [[nodiscard]] const cpu::PhysicalMemory& physical_memory() const noexcept;

    [[nodiscard]] cpu::MemoryBus& memory_bus() noexcept;
    [[nodiscard]] const cpu::MemoryBus& memory_bus() const noexcept;

    [[nodiscard]] std::size_t physical_memory_size_bytes() const noexcept;

private:
    cpu::PhysicalMemory physical_memory_;
    cpu::MemoryBus memory_bus_;
};
}
