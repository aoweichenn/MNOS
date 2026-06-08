#pragma once

#include <cstddef>
#include <cstdint>

#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/system/core_topology.hpp>
#include <mnos/os/dev/terminal.hpp>

namespace mnos::os::platform
{
class Machine final
{
public:
    explicit Machine(std::size_t physical_memory_size_bytes);
    Machine(std::size_t physical_memory_size_bytes, std::uint32_t core_count);

    [[nodiscard]] cpu::PhysicalMemory& physical_memory() noexcept;
    [[nodiscard]] const cpu::PhysicalMemory& physical_memory() const noexcept;

    [[nodiscard]] cpu::MemoryBus& memory_bus() noexcept;
    [[nodiscard]] const cpu::MemoryBus& memory_bus() const noexcept;

    [[nodiscard]] cpu::system::CoreTopology& core_topology() noexcept;
    [[nodiscard]] const cpu::system::CoreTopology& core_topology() const noexcept;
    [[nodiscard]] std::uint32_t processor_count() const noexcept;

    [[nodiscard]] dev::TerminalDevice& terminal_device() noexcept;
    [[nodiscard]] const dev::TerminalDevice& terminal_device() const noexcept;

    [[nodiscard]] std::size_t physical_memory_size_bytes() const noexcept;

private:
    cpu::PhysicalMemory physical_memory_;
    cpu::MemoryBus memory_bus_;
    cpu::system::CoreTopology core_topology_;
    dev::TerminalDevice terminal_device_;
};
}
