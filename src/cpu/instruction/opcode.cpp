#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/instruction/opcode.hpp>

namespace
{
constexpr std::string_view OPCODE_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";

class OpcodeCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::Opcode opcode) noexcept
    {
        return OPCODE_NAMES.contains(opcode);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::Opcode opcode) noexcept
    {
        return OPCODE_NAMES.index(opcode);
    }

    [[nodiscard]] static std::string_view assembly_name(const mnos::cpu::Opcode opcode) noexcept
    {
        return OPCODE_NAMES.name(opcode);
    }

private:
    inline static constexpr auto OPCODE_NAMES = mnos::core::make_enum_name_table<mnos::cpu::Opcode>(
        std::array<std::string_view, mnos::cpu::OPCODE_COUNT>{
            "MOV",
            "ADD",
            "SUB",
            "CMP",
            "JMP",
            "JE",
            "JNE",
            "HLT"},
        OPCODE_ASSEMBLY_NAME_INVALID_TEXT);
};
}

namespace mnos::cpu
{
bool is_opcode_valid(const Opcode opcode) noexcept
{
    return OpcodeCatalog::contains(opcode);
}

std::size_t opcode_to_index(const Opcode opcode) noexcept
{
    return OpcodeCatalog::index(opcode);
}

std::string_view opcode_to_assembly_name(const Opcode opcode) noexcept
{
    return OpcodeCatalog::assembly_name(opcode);
}
}
