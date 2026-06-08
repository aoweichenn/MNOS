#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/register/id.hpp>

namespace mnos::os::kernel
{
enum class SyscallNumber : std::uint64_t
{
    YIELD = 0,
    EXIT = 1,
    GETPID = 2,
    MAP_ANON_PAGE = 3,
    FORK_COW = 4,
    FUTEX_WAIT = 5,
    FUTEX_WAKE_ONE = 6,
    FUTEX_WAKE_ALL = 7,
    COUNT
};

enum class SyscallResult : std::uint8_t
{
    HANDLED,
    BLOCKED,
    UNSUPPORTED,
    INVALID_CONTEXT,
    INVALID_ARGUMENT,
    BAD_ADDRESS,
    OUT_OF_MEMORY,
    COUNT
};

enum class SyscallError : std::int64_t
{
    SUCCESS = 0,
    AGAIN = 11,
    NO_MEMORY = 12,
    BAD_ADDRESS = 14,
    ALREADY_EXISTS = 17,
    INVALID_ARGUMENT = 22,
    NO_SYS = 38,
    OPERATION_NOT_SUPPORTED = 95,
};

enum class SyscallArgument : std::uint8_t
{
    ARG0,
    ARG1,
    ARG2,
    ARG3,
    ARG4,
    ARG5,
    COUNT
};

inline constexpr std::size_t SYSCALL_NUMBER_COUNT = static_cast<std::size_t>(SyscallNumber::COUNT);
inline constexpr std::size_t SYSCALL_ARGUMENT_COUNT = static_cast<std::size_t>(SyscallArgument::COUNT);
inline constexpr cpu::Qword SYSCALL_SUCCESS_RESULT = cpu::Qword{0};
inline constexpr std::uint64_t SYSCALL_FORK_MAX_COW_PAGE_COUNT = std::uint64_t{256};
inline constexpr cpu::Qword SYSCALL_FUTEX_WORD_MASK = cpu::Qword{0xFFFF'FFFF};

[[nodiscard]] constexpr cpu::Qword syscall_error_result(const SyscallError error) noexcept
{
    return error == SyscallError::SUCCESS
        ? SYSCALL_SUCCESS_RESULT
        : static_cast<cpu::Qword>(-static_cast<std::int64_t>(error));
}

inline constexpr cpu::Qword SYSCALL_UNSUPPORTED_RESULT = syscall_error_result(SyscallError::NO_SYS);

class SyscallFrame final
{
public:
    explicit SyscallFrame(cpu::CpuState& cpu_state) noexcept;

    [[nodiscard]] cpu::Qword raw_number() const noexcept;
    [[nodiscard]] SyscallNumber number() const noexcept;
    [[nodiscard]] cpu::Qword argument(SyscallArgument argument) const;
    void set_result(cpu::Qword value) noexcept;
    void set_error(SyscallError error) noexcept;

private:
    cpu::CpuState* cpu_state_;
};

[[nodiscard]] bool is_syscall_number_valid(SyscallNumber number) noexcept;
[[nodiscard]] std::size_t syscall_number_to_index(SyscallNumber number) noexcept;
[[nodiscard]] std::string_view syscall_number_to_name(SyscallNumber number) noexcept;
[[nodiscard]] SyscallNumber syscall_number_from_raw(cpu::Qword raw_number) noexcept;
[[nodiscard]] bool is_syscall_argument_valid(SyscallArgument argument) noexcept;
[[nodiscard]] std::size_t syscall_argument_to_index(SyscallArgument argument) noexcept;
[[nodiscard]] cpu::RegisterId syscall_argument_register(SyscallArgument argument);
}
