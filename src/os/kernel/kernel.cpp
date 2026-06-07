#include <stdexcept>

#include <mnos/os/kernel/kernel.hpp>

namespace
{
constexpr const char* KERNEL_ALREADY_BOOTED_MESSAGE = "kernel has already booted";
constexpr const char* KERNEL_INSUFFICIENT_MEMORY_MESSAGE = "kernel requires at least one physical page";
}

namespace mnos::os::kernel
{
Kernel::Kernel(BootContext& boot_context) noexcept : boot_context_(&boot_context)
{
}

void Kernel::boot()
{
    if (this->booted_)
    {
        throw std::logic_error{KERNEL_ALREADY_BOOTED_MESSAGE};
    }

    if (this->boot_context_->physical_page_count() < KERNEL_MIN_BOOTABLE_PAGE_COUNT)
    {
        throw std::runtime_error{KERNEL_INSUFFICIENT_MEMORY_MESSAGE};
    }

    this->booted_ = true;
}

bool Kernel::is_booted() const noexcept
{
    return this->booted_;
}

BootContext& Kernel::boot_context() noexcept
{
    return *this->boot_context_;
}

const BootContext& Kernel::boot_context() const noexcept
{
    return *this->boot_context_;
}

std::size_t Kernel::physical_memory_size_bytes() const noexcept
{
    return this->boot_context_->physical_memory_size_bytes();
}

std::uint64_t Kernel::physical_page_count() const noexcept
{
    return this->boot_context_->physical_page_count();
}

std::uint32_t Kernel::bootstrap_processor_count() const noexcept
{
    return this->boot_context_->bootstrap_processor_count();
}
}
