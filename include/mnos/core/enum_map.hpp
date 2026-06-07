#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace mnos
{
template <typename Enum>
concept EnumKey = std::is_enum_v<Enum>;

template <EnumKey Enum>
[[nodiscard]] constexpr std::size_t enum_to_index(const Enum value) noexcept
{
    return static_cast<std::size_t>(value);
}

template <EnumKey Enum, typename Value, std::size_t COUNT>
class EnumMap
{
public:
    using key_type = Enum;
    using mapped_type = Value;
    using storage_type = std::array<Value, COUNT>;

    constexpr explicit EnumMap(storage_type values) noexcept(std::is_nothrow_move_constructible_v<storage_type>) :
        values_(std::move(values))
    {
    }

    [[nodiscard]] static constexpr std::size_t size() noexcept
    {
        return COUNT;
    }

    [[nodiscard]] constexpr bool contains(const Enum key) const noexcept
    {
        return enum_to_index(key) < COUNT;
    }

    [[nodiscard]] constexpr const Value* find(const Enum key) const noexcept
    {
        if (!this->contains(key))
        {
            return nullptr;
        }
        return &this->values_[enum_to_index(key)];
    }

    [[nodiscard]] constexpr const Value& at(const Enum key, const char* const error_message) const
    {
        const Value* const value = this->find(key);
        if (value == nullptr)
        {
            throw std::out_of_range{error_message};
        }
        return *value;
    }

    [[nodiscard]] constexpr Value value_or(const Enum key, const Value fallback) const noexcept(
        std::is_nothrow_copy_constructible_v<Value>)
    {
        const Value* const value = this->find(key);
        if (value == nullptr)
        {
            return fallback;
        }
        return *value;
    }

private:
    storage_type values_;
};

template <EnumKey Enum, typename Value, std::size_t COUNT>
[[nodiscard]] constexpr auto make_enum_map(std::array<Value, COUNT> values) noexcept(
    std::is_nothrow_move_constructible_v<std::array<Value, COUNT>>) -> EnumMap<Enum, Value, COUNT>
{
    return EnumMap<Enum, Value, COUNT>{std::move(values)};
}
}
