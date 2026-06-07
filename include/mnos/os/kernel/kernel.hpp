#pragma once

#include <cstddef>
#include <cstdint>

#include <mnos/os/kernel/boot_context.hpp>

namespace mnos::os::kernel
{
inline constexpr std::uint64_t KERNEL_MIN_BOOTABLE_PAGE_COUNT = std::uint64_t{1};

class Kernel final
{
public:
    explicit Kernel(BootContext& boot_context) noexcept;

    void boot();

    [[nodiscard]] bool is_booted() const noexcept;
    [[nodiscard]] BootContext& boot_context() noexcept;
    [[nodiscard]] const BootContext& boot_context() const noexcept;
    [[nodiscard]] std::size_t physical_memory_size_bytes() const noexcept;
    [[nodiscard]] std::uint64_t physical_page_count() const noexcept;
    [[nodiscard]] std::uint32_t bootstrap_processor_count() const noexcept;

private:
    BootContext* boot_context_;
    bool booted_ = false;
};
}
