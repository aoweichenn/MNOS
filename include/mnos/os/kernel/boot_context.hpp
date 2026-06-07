#pragma once

#include <cstddef>
#include <cstdint>

#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/os/platform/machine.hpp>

namespace mnos::os::kernel
{
inline constexpr std::uint32_t BOOT_CONTEXT_DEFAULT_BOOTSTRAP_PROCESSOR_COUNT = std::uint32_t{1};

class BootContext final
{
public:
    explicit BootContext(platform::Machine& machine);
    BootContext(platform::Machine& machine, std::uint32_t bootstrap_processor_count);

    [[nodiscard]] platform::Machine& machine() noexcept;
    [[nodiscard]] const platform::Machine& machine() const noexcept;

    [[nodiscard]] cpu::PhysicalMemory& physical_memory() noexcept;
    [[nodiscard]] const cpu::PhysicalMemory& physical_memory() const noexcept;

    [[nodiscard]] cpu::MemoryBus& memory_bus() noexcept;
    [[nodiscard]] const cpu::MemoryBus& memory_bus() const noexcept;

    [[nodiscard]] std::size_t physical_memory_size_bytes() const noexcept;
    [[nodiscard]] std::uint64_t physical_page_count() const noexcept;
    [[nodiscard]] std::uint32_t bootstrap_processor_count() const noexcept;

private:
    platform::Machine* machine_;
    std::uint32_t bootstrap_processor_count_;
};
}
