#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/instruction/condition_code.hpp>

namespace
{
constexpr std::string_view CONDITION_CODE_SUFFIX_INVALID_TEXT = "<invalid>";

class ConditionCodeCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::ConditionCode condition) noexcept
    {
        return CONDITION_CODE_SUFFIXES.contains(condition);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::ConditionCode condition) noexcept
    {
        return CONDITION_CODE_SUFFIXES.index(condition);
    }

    [[nodiscard]] static std::string_view assembly_suffix(const mnos::cpu::ConditionCode condition) noexcept
    {
        return CONDITION_CODE_SUFFIXES.name(condition);
    }

private:
    inline static constexpr auto CONDITION_CODE_SUFFIXES =
        mnos::core::make_enum_name_table<mnos::cpu::ConditionCode>(
            std::array<std::string_view, mnos::cpu::CONDITION_CODE_COUNT>{
                "O",
                "NO",
                "B",
                "AE",
                "E",
                "NE",
                "BE",
                "A",
                "S",
                "NS",
                "P",
                "NP",
                "L",
                "GE",
                "LE",
                "G"},
            CONDITION_CODE_SUFFIX_INVALID_TEXT);
};
}

namespace mnos::cpu
{
bool is_condition_code_valid(const ConditionCode condition) noexcept
{
    return ConditionCodeCatalog::contains(condition);
}

std::size_t condition_code_to_index(const ConditionCode condition) noexcept
{
    return ConditionCodeCatalog::index(condition);
}

std::string_view condition_code_to_assembly_suffix(const ConditionCode condition) noexcept
{
    return ConditionCodeCatalog::assembly_suffix(condition);
}
}
