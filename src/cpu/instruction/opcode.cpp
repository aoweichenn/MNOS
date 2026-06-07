#include <array>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/instruction/opcode.hpp>

namespace
{
constexpr std::string_view OPCODE_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr auto OPCODE_INSTRUCTION_ASSEMBLY_NAME_TABLE = mnos::make_enum_map<mnos::Opcode>(
    std::array<std::string_view, mnos::OPCODE_INSTRUCTION_KIND_COUNT>{
        "MOV",
        "ADD",
        "SUB",
        "CMP",
        "JMP",
        "JE",
        "JNE",
        "HALT"});
}

namespace mnos
{
bool is_opcode_valid(const Opcode opcode) noexcept
{
    return OPCODE_INSTRUCTION_ASSEMBLY_NAME_TABLE.contains(opcode);
}

std::size_t opcode_to_index(const Opcode opcode) noexcept
{
    return enum_to_index(opcode);
}

std::string_view opcode_to_assembly_name(const Opcode opcode) noexcept
{
    return OPCODE_INSTRUCTION_ASSEMBLY_NAME_TABLE.value_or(opcode, OPCODE_ASSEMBLY_NAME_INVALID_TEXT);
}
}
