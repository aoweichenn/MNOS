//
// Created by aoweichen on 2026/6/6.
//
#include <array>
#include <mnos/cpu/instruction/opcode.hpp>

namespace
{
constexpr std::string_view OPCODE_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr std::array<std::string_view, mnos::OPCODE_INSTRUCTION_KIND_COUNT> OPCODE_INSTRUCTION_ASSEMBLY_NAME_TABLE{
    "MOV",
    "ADD",
    "SUB",
    "CMP",
    "JMP",
    "JE",
    "JNE",
    "HALT"
};

[[nodiscard]] constexpr std::size_t to_index(mnos::Opcode opcode) noexcept
{
    return static_cast<std::size_t>(opcode);
}
}

namespace mnos
{
bool is_opcode_valid(const Opcode opcode) noexcept
{
    return to_index(opcode) < OPCODE_INSTRUCTION_KIND_COUNT;
}

std::size_t opcode_to_index(const Opcode opcode) noexcept
{
    return to_index(opcode);
}

std::string_view opcode_to_assembly_name(Opcode opcode) noexcept
{
    if (!is_opcode_valid(opcode))
    {
        return OPCODE_ASSEMBLY_NAME_INVALID_TEXT;
    }
    return OPCODE_INSTRUCTION_ASSEMBLY_NAME_TABLE[to_index(opcode)];
}
}
