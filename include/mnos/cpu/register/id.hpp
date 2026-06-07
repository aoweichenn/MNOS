#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos
{
enum class RegisterId : std::uint8_t
{
    RAX,
    RBX,
    RCX,
    RDX,
    RSI,
    RDI,
    RBP,
    RSP,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    COUNT
};

inline constexpr std::size_t REGISTER_ID_GENERAL_REGISTER_COUNT = static_cast<std::size_t>(RegisterId::COUNT);

[[nodiscard]] bool is_register_id_valid(RegisterId id) noexcept;

[[nodiscard]] std::size_t register_id_to_index(RegisterId id) noexcept;

[[nodiscard]] std::string_view register_id_to_assembly_name(RegisterId id) noexcept;
}
