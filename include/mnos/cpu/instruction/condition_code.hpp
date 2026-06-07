#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos::cpu
{
enum class ConditionCode : std::uint8_t
{
    O,
    NO,
    B,
    AE,
    E,
    NE,
    BE,
    A,
    S,
    NS,
    P,
    NP,
    L,
    GE,
    LE,
    G,
    COUNT
};

inline constexpr std::size_t CONDITION_CODE_COUNT = static_cast<std::size_t>(ConditionCode::COUNT);

[[nodiscard]] bool is_condition_code_valid(ConditionCode condition) noexcept;

[[nodiscard]] std::size_t condition_code_to_index(ConditionCode condition) noexcept;

[[nodiscard]] std::string_view condition_code_to_assembly_suffix(ConditionCode condition) noexcept;
}
