#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <mnos/cpu/common/types.hpp>

namespace mnos::os::kernel
{
enum class SyscallNumber : std::uint64_t
{
    YIELD = 0,
    EXIT = 1,
    COUNT
};

enum class SyscallResult : std::uint8_t
{
    HANDLED,
    UNSUPPORTED,
    COUNT
};

inline constexpr std::size_t SYSCALL_NUMBER_COUNT = static_cast<std::size_t>(SyscallNumber::COUNT);
inline constexpr cpu::Qword SYSCALL_SUCCESS_RESULT = cpu::Qword{0};
inline constexpr cpu::Qword SYSCALL_UNSUPPORTED_RESULT = ~cpu::Qword{0};

[[nodiscard]] bool is_syscall_number_valid(SyscallNumber number) noexcept;
[[nodiscard]] std::size_t syscall_number_to_index(SyscallNumber number) noexcept;
[[nodiscard]] std::string_view syscall_number_to_name(SyscallNumber number) noexcept;
[[nodiscard]] SyscallNumber syscall_number_from_raw(cpu::Qword raw_number) noexcept;
}
