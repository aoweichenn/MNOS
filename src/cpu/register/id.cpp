#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/register/id.hpp>

namespace
{
constexpr std::string_view REGISTER_ID_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";

class RegisterIdCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::RegisterId id) noexcept
    {
        return REGISTER_ID_NAMES.contains(id);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::RegisterId id) noexcept
    {
        return REGISTER_ID_NAMES.index(id);
    }

    [[nodiscard]] static std::string_view assembly_name(const mnos::cpu::RegisterId id) noexcept
    {
        return REGISTER_ID_NAMES.name(id);
    }

private:
    inline static constexpr auto REGISTER_ID_NAMES = mnos::core::make_enum_name_table<mnos::cpu::RegisterId>(
        std::array<std::string_view, mnos::cpu::REGISTER_ID_GENERAL_REGISTER_COUNT>{
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
            "R15"},
        REGISTER_ID_ASSEMBLY_NAME_INVALID_TEXT);
};
}

namespace mnos::cpu
{
bool is_register_id_valid(const RegisterId id) noexcept
{
    return RegisterIdCatalog::contains(id);
}

std::size_t register_id_to_index(const RegisterId id) noexcept
{
    return RegisterIdCatalog::index(id);
}

std::string_view register_id_to_assembly_name(const RegisterId id) noexcept
{
    return RegisterIdCatalog::assembly_name(id);
}
}
