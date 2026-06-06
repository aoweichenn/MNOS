//
// Created by aoweichen on 2026/6/5.
//
#include <array>
#include <mnos/cpu/register/id.hpp>

namespace
{
constexpr std::string_view REGISTER_ID_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr std::array<std::string_view, mnos::REGISTER_ID_GENERAL_REGISTER_COUNT>
REGISTER_ID_GENERAL_REGISTER_ASSEMBLY_NAME_TABLE{
    "RAX",
    "RBX",
    "RCX",
    "RDX",
    "RSI",
    "RDI",
    "RBP",
    "RSP",
    "R8",
    "R9",
    "R10",
    "R11",
    "R12",
    "R13",
    "R14",
    "R15"
};
}

namespace mnos
{
bool is_register_id_valid(const RegisterId id) noexcept
{
    return register_id_to_index(id) < REGISTER_ID_GENERAL_REGISTER_COUNT;
}

std::size_t register_id_to_index(RegisterId id) noexcept
{
    return static_cast<std::size_t>(id);
}

std::string_view register_id_to_assembly_name(RegisterId id) noexcept
{
    if (!is_register_id_valid(id))
    {
        return REGISTER_ID_ASSEMBLY_NAME_INVALID_TEXT;
    }
    return REGISTER_ID_GENERAL_REGISTER_ASSEMBLY_NAME_TABLE[register_id_to_index(id)];
}
}

