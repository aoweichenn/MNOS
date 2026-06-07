#pragma once

#include <cstdint>

namespace mnos::cpu::system
{
inline constexpr std::uint32_t CORE_ID_BOOTSTRAP_VALUE = std::uint32_t{0};

class CoreId final
{
public:
    using value_type = std::uint32_t;

    constexpr CoreId() noexcept = default;
    explicit constexpr CoreId(value_type value) noexcept;

    [[nodiscard]] static constexpr CoreId bootstrap() noexcept;

    [[nodiscard]] constexpr value_type value() const noexcept;

private:
    value_type value_ = CORE_ID_BOOTSTRAP_VALUE;
};

[[nodiscard]] constexpr bool operator==(CoreId left, CoreId right) noexcept
{
    return left.value() == right.value();
}

[[nodiscard]] constexpr bool operator!=(CoreId left, CoreId right) noexcept
{
    return !(left == right);
}

[[nodiscard]] constexpr bool operator<(CoreId left, CoreId right) noexcept
{
    return left.value() < right.value();
}

constexpr CoreId::CoreId(const value_type value) noexcept : value_(value)
{
}

constexpr CoreId CoreId::bootstrap() noexcept
{
    return CoreId{CORE_ID_BOOTSTRAP_VALUE};
}

constexpr CoreId::value_type CoreId::value() const noexcept
{
    return this->value_;
}
}
