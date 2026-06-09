#include <array>
#include <stdexcept>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/os/kernel/syscall.hpp>

namespace
{
constexpr std::string_view SYSCALL_NUMBER_INVALID_NAME = "<invalid>";
constexpr const char* SYSCALL_ARGUMENT_INVALID_MESSAGE = "syscall argument index is invalid";

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
        std::array<std::string_view, mnos::os::kernel::SYSCALL_NUMBER_COUNT>{
            "YIELD",
            "EXIT",
            "GETPID",
            "MAP_ANON_PAGE",
            "FORK_COW",
            "FUTEX_WAIT",
            "FUTEX_WAKE_ONE",
            "FUTEX_WAKE_ALL",
            "READ",
            "WRITE",
            "OPEN",
            "CLOSE",
            "STAT",
            "READDIR",
            "WAIT"},
        SYSCALL_NUMBER_INVALID_NAME);
};

class SyscallArgumentCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::os::kernel::SyscallArgument argument) noexcept
    {
        return REGISTERS.contains(argument);
    }

    [[nodiscard]] static std::size_t index(const mnos::os::kernel::SyscallArgument argument) noexcept
    {
        return mnos::core::enum_to_index(argument);
    }

    [[nodiscard]] static mnos::cpu::RegisterId reg(const mnos::os::kernel::SyscallArgument argument)
    {
        return REGISTERS.at(argument, SYSCALL_ARGUMENT_INVALID_MESSAGE);
    }

private:
    inline static constexpr auto REGISTERS = mnos::core::make_enum_map<mnos::os::kernel::SyscallArgument>(
        std::array<mnos::cpu::RegisterId, mnos::os::kernel::SYSCALL_ARGUMENT_COUNT>{
            mnos::cpu::RegisterId::RDI,
            mnos::cpu::RegisterId::RSI,
            mnos::cpu::RegisterId::RDX,
            mnos::cpu::RegisterId::R10,
            mnos::cpu::RegisterId::R8,
            mnos::cpu::RegisterId::R9});
};
}

namespace mnos::os::kernel
{
SyscallFrame::SyscallFrame(cpu::CpuState& cpu_state) noexcept : cpu_state_(&cpu_state)
{
}

cpu::Qword SyscallFrame::raw_number() const noexcept
{
    return this->cpu_state_->registers().read(cpu::RegisterId::RAX);
}

SyscallNumber SyscallFrame::number() const noexcept
{
    return syscall_number_from_raw(this->raw_number());
}

cpu::Qword SyscallFrame::argument(const SyscallArgument argument) const
{
    return this->cpu_state_->registers().read(syscall_argument_register(argument));
}

void SyscallFrame::set_result(const cpu::Qword value) noexcept
{
    this->cpu_state_->registers().write(cpu::RegisterId::RAX, value);
}

void SyscallFrame::set_error(const SyscallError error) noexcept
{
    this->set_result(syscall_error_result(error));
}

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

bool is_syscall_argument_valid(const SyscallArgument argument) noexcept
{
    return SyscallArgumentCatalog::contains(argument);
}

std::size_t syscall_argument_to_index(const SyscallArgument argument) noexcept
{
    return SyscallArgumentCatalog::index(argument);
}

cpu::RegisterId syscall_argument_register(const SyscallArgument argument)
{
    return SyscallArgumentCatalog::reg(argument);
}
}
