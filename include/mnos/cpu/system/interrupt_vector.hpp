#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos::cpu::system
{
inline constexpr std::size_t INTERRUPT_VECTOR_COUNT = 256;
inline constexpr std::uint8_t INTERRUPT_VECTOR_DIVIDE_ERROR_VALUE = 0;
inline constexpr std::uint8_t INTERRUPT_VECTOR_BREAKPOINT_VALUE = 3;
inline constexpr std::uint8_t INTERRUPT_VECTOR_INVALID_OPCODE_VALUE = 6;
inline constexpr std::uint8_t INTERRUPT_VECTOR_GENERAL_PROTECTION_VALUE = 13;
inline constexpr std::uint8_t INTERRUPT_VECTOR_PAGE_FAULT_VALUE = 14;
inline constexpr std::uint8_t INTERRUPT_VECTOR_TIMER_VALUE = 32;
inline constexpr std::uint8_t INTERRUPT_VECTOR_SYSCALL_COMPAT_VALUE = 0x80;

class InterruptVector final
{
public:
    using value_type = std::uint8_t;

    constexpr InterruptVector() noexcept = default;
    constexpr explicit InterruptVector(value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] static constexpr InterruptVector divide_error() noexcept
    {
        return InterruptVector{INTERRUPT_VECTOR_DIVIDE_ERROR_VALUE};
    }

    [[nodiscard]] static constexpr InterruptVector breakpoint() noexcept
    {
        return InterruptVector{INTERRUPT_VECTOR_BREAKPOINT_VALUE};
    }

    [[nodiscard]] static constexpr InterruptVector invalid_opcode() noexcept
    {
        return InterruptVector{INTERRUPT_VECTOR_INVALID_OPCODE_VALUE};
    }

    [[nodiscard]] static constexpr InterruptVector general_protection() noexcept
    {
        return InterruptVector{INTERRUPT_VECTOR_GENERAL_PROTECTION_VALUE};
    }

    [[nodiscard]] static constexpr InterruptVector page_fault() noexcept
    {
        return InterruptVector{INTERRUPT_VECTOR_PAGE_FAULT_VALUE};
    }

    [[nodiscard]] static constexpr InterruptVector timer() noexcept
    {
        return InterruptVector{INTERRUPT_VECTOR_TIMER_VALUE};
    }

    [[nodiscard]] static constexpr InterruptVector syscall_compat() noexcept
    {
        return InterruptVector{INTERRUPT_VECTOR_SYSCALL_COMPAT_VALUE};
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] constexpr std::size_t index() const noexcept
    {
        return static_cast<std::size_t>(this->value_);
    }

    [[nodiscard]] constexpr bool operator==(const InterruptVector& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const InterruptVector& other) const noexcept
    {
        return this->value_ < other.value_;
    }

private:
    value_type value_ = INTERRUPT_VECTOR_DIVIDE_ERROR_VALUE;
};

[[nodiscard]] std::string_view interrupt_vector_to_name(InterruptVector vector) noexcept;
}
