#pragma once

#include <compare>
#include <cstdint>

namespace mnos::os::sched
{
inline constexpr std::uint64_t THREAD_ID_INVALID_VALUE = std::uint64_t{0};
inline constexpr std::uint64_t THREAD_ID_FIRST_KERNEL_VALUE = std::uint64_t{1};

class ThreadId final
{
public:
    using value_type = std::uint64_t;

    constexpr ThreadId() noexcept = default;

    explicit constexpr ThreadId(const value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] static constexpr ThreadId invalid() noexcept
    {
        return ThreadId{};
    }

    [[nodiscard]] static constexpr ThreadId first_kernel_thread() noexcept
    {
        return ThreadId{THREAD_ID_FIRST_KERNEL_VALUE};
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return this->value_ != THREAD_ID_INVALID_VALUE;
    }

    [[nodiscard]] friend constexpr auto operator<=>(const ThreadId&, const ThreadId&) noexcept = default;

private:
    value_type value_ = THREAD_ID_INVALID_VALUE;
};
}
