#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/os/kernel/syscall.hpp>

namespace
{
constexpr std::string_view SYSCALL_NUMBER_INVALID_NAME = "<invalid>";

class SyscallNumberCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::os::kernel::SyscallNumber number) noexcept
    {
        return NAMES.contains(number);
    }

    [[nodiscard]] static std::size_t index(const mnos::os::kernel::SyscallNumber number) noexcept
    {
        return NAMES.index(number);
    }

    [[nodiscard]] static std::string_view name(const mnos::os::kernel::SyscallNumber number) noexcept
    {
        return NAMES.name(number);
    }

private:
    inline static constexpr auto NAMES = mnos::core::make_enum_name_table<mnos::os::kernel::SyscallNumber>(
        std::array<std::string_view, mnos::os::kernel::SYSCALL_NUMBER_COUNT>{"YIELD", "EXIT"},
        SYSCALL_NUMBER_INVALID_NAME);
};
}

namespace mnos::os::kernel
{
bool is_syscall_number_valid(const SyscallNumber number) noexcept
{
    return SyscallNumberCatalog::contains(number);
}

std::size_t syscall_number_to_index(const SyscallNumber number) noexcept
{
    return SyscallNumberCatalog::index(number);
}

std::string_view syscall_number_to_name(const SyscallNumber number) noexcept
{
    return SyscallNumberCatalog::name(number);
}

SyscallNumber syscall_number_from_raw(const cpu::Qword raw_number) noexcept
{
    if (raw_number >= static_cast<cpu::Qword>(SYSCALL_NUMBER_COUNT))
    {
        return SyscallNumber::COUNT;
    }
    return static_cast<SyscallNumber>(raw_number);
}
}
