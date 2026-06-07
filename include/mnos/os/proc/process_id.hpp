#pragma once

#include <compare>
#include <cstdint>

namespace mnos::os::proc
{
inline constexpr std::uint64_t PROCESS_ID_INVALID_VALUE = std::uint64_t{0};
inline constexpr std::uint64_t PROCESS_ID_FIRST_USER_VALUE = std::uint64_t{1};

class ProcessId final
{
public:
    using value_type = std::uint64_t;

    constexpr ProcessId() noexcept = default;

    explicit constexpr ProcessId(const value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] static constexpr ProcessId invalid() noexcept
    {
        return ProcessId{};
    }

    [[nodiscard]] static constexpr ProcessId first_user_process() noexcept
    {
        return ProcessId{PROCESS_ID_FIRST_USER_VALUE};
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return this->value_ != PROCESS_ID_INVALID_VALUE;
    }

    [[nodiscard]] friend constexpr auto operator<=>(const ProcessId&, const ProcessId&) noexcept = default;

private:
    value_type value_ = PROCESS_ID_INVALID_VALUE;
};
}
