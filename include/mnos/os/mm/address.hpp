#pragma once

#include <compare>
#include <cstdint>

#include <mnos/cpu/common/types.hpp>

namespace mnos::os::mm
{
using AddressValue = std::uint64_t;

inline constexpr AddressValue ADDRESS_ZERO_VALUE = AddressValue{0};

struct PhysicalAddressTag
{
};

struct VirtualAddressTag
{
};

template <typename Tag>
class BasicAddress final
{
public:
    using value_type = AddressValue;

    constexpr BasicAddress() noexcept = default;

    explicit constexpr BasicAddress(const value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] static constexpr BasicAddress zero() noexcept
    {
        return BasicAddress{};
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] constexpr bool is_zero() const noexcept
    {
        return this->value_ == ADDRESS_ZERO_VALUE;
    }

    constexpr BasicAddress& operator+=(const value_type byte_count) noexcept
    {
        this->value_ += byte_count;
        return *this;
    }

    constexpr BasicAddress& operator-=(const value_type byte_count) noexcept
    {
        this->value_ -= byte_count;
        return *this;
    }

    [[nodiscard]] friend constexpr BasicAddress operator+(
        BasicAddress address,
        const value_type byte_count) noexcept
    {
        address += byte_count;
        return address;
    }

    [[nodiscard]] friend constexpr BasicAddress operator-(
        BasicAddress address,
        const value_type byte_count) noexcept
    {
        address -= byte_count;
        return address;
    }

    [[nodiscard]] friend constexpr value_type operator-(
        const BasicAddress left,
        const BasicAddress right) noexcept
    {
        return left.value_ - right.value_;
    }

    [[nodiscard]] friend constexpr auto operator<=>(const BasicAddress&, const BasicAddress&) noexcept = default;

private:
    value_type value_ = ADDRESS_ZERO_VALUE;
};

using PhysicalAddress = BasicAddress<PhysicalAddressTag>;
using VirtualAddress = BasicAddress<VirtualAddressTag>;

[[nodiscard]] constexpr cpu::Address64 to_cpu_address(const PhysicalAddress address) noexcept
{
    return static_cast<cpu::Address64>(address.value());
}

[[nodiscard]] constexpr cpu::Address64 to_cpu_address(const VirtualAddress address) noexcept
{
    return static_cast<cpu::Address64>(address.value());
}
}
