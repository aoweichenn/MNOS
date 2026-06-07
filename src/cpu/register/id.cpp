#include <array>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/register/id.hpp>

namespace
{
constexpr std::string_view REGISTER_ID_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr auto REGISTER_ID_GENERAL_REGISTER_ASSEMBLY_NAME_TABLE = mnos::make_enum_map<mnos::RegisterId>(
    std::array<std::string_view, mnos::REGISTER_ID_GENERAL_REGISTER_COUNT>{
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
        "R15"});
}

namespace mnos
{
bool is_register_id_valid(const RegisterId id) noexcept
{
    return REGISTER_ID_GENERAL_REGISTER_ASSEMBLY_NAME_TABLE.contains(id);
}

std::size_t register_id_to_index(const RegisterId id) noexcept
{
    return enum_to_index(id);
}

std::string_view register_id_to_assembly_name(const RegisterId id) noexcept
{
    return REGISTER_ID_GENERAL_REGISTER_ASSEMBLY_NAME_TABLE.value_or(id, REGISTER_ID_ASSEMBLY_NAME_INVALID_TEXT);
}
}
