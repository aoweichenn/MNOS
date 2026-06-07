#include <array>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/flags/id.hpp>

namespace
{
constexpr std::string_view FLAG_ID_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr const char* FLAG_ID_ACCESS_INVALID_ID_MESSAGE = "flag id is invalid";

constexpr auto FLAG_ID_BIT_INDEX_TABLE = mnos::make_enum_map<mnos::FlagId>(
    std::array<std::size_t, mnos::FLAG_ID_STATUS_FLAG_COUNT>{
        mnos::FLAG_ID_CF_BIT_INDEX,
        mnos::FLAG_ID_ZF_BIT_INDEX,
        mnos::FLAG_ID_SF_BIT_INDEX,
        mnos::FLAG_ID_OF_BIT_INDEX});

constexpr auto FLAG_ID_ASSEMBLY_NAME_TABLE = mnos::make_enum_map<mnos::FlagId>(
    std::array<std::string_view, mnos::FLAG_ID_STATUS_FLAG_COUNT>{"CF", "ZF", "SF", "OF"});
}

namespace mnos
{
bool is_flag_id_valid(const FlagId id) noexcept
{
    return FLAG_ID_BIT_INDEX_TABLE.contains(id);
}

std::size_t flag_id_to_index(const FlagId id) noexcept
{
    return enum_to_index(id);
}

std::size_t flag_id_to_bit_index(const FlagId id)
{
    return FLAG_ID_BIT_INDEX_TABLE.at(id, FLAG_ID_ACCESS_INVALID_ID_MESSAGE);
}

std::string_view flag_id_to_assembly_name(const FlagId id) noexcept
{
    return FLAG_ID_ASSEMBLY_NAME_TABLE.value_or(id, FLAG_ID_ASSEMBLY_NAME_INVALID_TEXT);
}
}
