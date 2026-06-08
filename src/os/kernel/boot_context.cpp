#include <stdexcept>

#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/mm/page.hpp>

namespace
{
constexpr const char* BOOT_CONTEXT_EMPTY_MEMORY_MESSAGE = "boot context requires physical memory";
constexpr const char* BOOT_CONTEXT_ZERO_PROCESSOR_COUNT_MESSAGE = "boot context requires at least one processor";
constexpr const char* BOOT_CONTEXT_PROCESSOR_COUNT_EXCEEDS_MACHINE_MESSAGE =
    "boot context processor count exceeds machine topology";
}

namespace mnos::os::kernel
{
BootContext::BootContext(platform::Machine& machine) :
    BootContext(machine, BOOT_CONTEXT_DEFAULT_BOOTSTRAP_PROCESSOR_COUNT)
{
}

BootContext::BootContext(platform::Machine& machine, const std::uint32_t bootstrap_processor_count) :
    machine_(&machine), bootstrap_processor_count_(bootstrap_processor_count)
{
    if (this->machine_->physical_memory_size_bytes() == std::size_t{0})
    {
        throw std::invalid_argument{BOOT_CONTEXT_EMPTY_MEMORY_MESSAGE};
    }

    if (this->bootstrap_processor_count_ == std::uint32_t{0})
    {
        throw std::invalid_argument{BOOT_CONTEXT_ZERO_PROCESSOR_COUNT_MESSAGE};
    }

    if (this->bootstrap_processor_count_ > this->machine_->processor_count())
    {
        throw std::invalid_argument{BOOT_CONTEXT_PROCESSOR_COUNT_EXCEEDS_MACHINE_MESSAGE};
    }
}

platform::Machine& BootContext::machine() noexcept
{
    return *this->machine_;
}

const platform::Machine& BootContext::machine() const noexcept
{
    return *this->machine_;
}

cpu::PhysicalMemory& BootContext::physical_memory() noexcept
{
    return this->machine_->physical_memory();
}

const cpu::PhysicalMemory& BootContext::physical_memory() const noexcept
{
    return this->machine_->physical_memory();
}

cpu::MemoryBus& BootContext::memory_bus() noexcept
{
    return this->machine_->memory_bus();
}

const cpu::MemoryBus& BootContext::memory_bus() const noexcept
{
    return this->machine_->memory_bus();
}

dev::TerminalDevice& BootContext::terminal_device() noexcept
{
    return this->machine_->terminal_device();
}

const dev::TerminalDevice& BootContext::terminal_device() const noexcept
{
    return this->machine_->terminal_device();
}

std::size_t BootContext::physical_memory_size_bytes() const noexcept
{
    return this->machine_->physical_memory_size_bytes();
}

std::uint64_t BootContext::physical_page_count() const noexcept
{
    return static_cast<std::uint64_t>(this->physical_memory_size_bytes()) / mm::MM_PAGE_SIZE_BYTES;
}

std::uint32_t BootContext::bootstrap_processor_count() const noexcept
{
    return this->bootstrap_processor_count_;
}
}
