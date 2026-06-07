#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos::cpu
{
enum class FlagId : std::uint8_t
{
    CF,    // Carry Flag
    PF,    // Parity Flag
    ZF,    // Zero Flag
    SF,    // Sign Flag
    OF,    // Overflow Flag
    COUNT, // flags count
};

inline constexpr std::size_t FLAG_ID_COUNT = static_cast<std::size_t>(FlagId::COUNT);
inline constexpr std::size_t FLAG_ID_CF_BIT_INDEX = 0;
inline constexpr std::size_t FLAG_ID_PF_BIT_INDEX = 2;
inline constexpr std::size_t FLAG_ID_ZF_BIT_INDEX = 6;
inline constexpr std::size_t FLAG_ID_SF_BIT_INDEX = 7;
inline constexpr std::size_t FLAG_ID_OF_BIT_INDEX = 11;

[[nodiscard]] bool is_flag_id_valid(FlagId id) noexcept;
[[nodiscard]] std::size_t flag_id_to_index(FlagId id) noexcept;
[[nodiscard]] std::size_t flag_id_to_bit_index(FlagId id);
[[nodiscard]] std::string_view flag_id_to_assembly_name(FlagId id) noexcept;
}
