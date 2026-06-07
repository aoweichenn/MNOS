#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos::cpu::system
{
using RingNumber = std::uint8_t;

enum class PrivilegeLevel : std::uint8_t
{
    RING0,
    RING1,
    RING2,
    RING3,
    COUNT,
};

inline constexpr std::size_t PRIVILEGE_LEVEL_COUNT = static_cast<std::size_t>(PrivilegeLevel::COUNT);
inline constexpr RingNumber PRIVILEGE_LEVEL_RING0_NUMBER = RingNumber{0};
inline constexpr RingNumber PRIVILEGE_LEVEL_RING1_NUMBER = RingNumber{1};
inline constexpr RingNumber PRIVILEGE_LEVEL_RING2_NUMBER = RingNumber{2};
inline constexpr RingNumber PRIVILEGE_LEVEL_RING3_NUMBER = RingNumber{3};

[[nodiscard]] bool is_privilege_level_valid(PrivilegeLevel level) noexcept;
[[nodiscard]] std::size_t privilege_level_to_index(PrivilegeLevel level) noexcept;
[[nodiscard]] RingNumber privilege_level_to_ring(PrivilegeLevel level);
[[nodiscard]] std::string_view privilege_level_to_name(PrivilegeLevel level) noexcept;
[[nodiscard]] bool is_more_privileged(PrivilegeLevel target, PrivilegeLevel current) noexcept;
}
