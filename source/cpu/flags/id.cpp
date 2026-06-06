//
// Created by aoweichen on 2026/6/6.
//
#include <array>
#include <stdexcept>
#include <mnos/cpu/flags/id.hpp>

namespace
{
constexpr std::string_view FLAG_ID_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr const char* FLAG_ID_ACCESS_INVALID_ID_MESSAGE = "flag id is invalid";
constexpr std::array<std::size_t, mnos::FLAG_ID_STATUS_FLAG_COUNT> FLAG_ID_BIT_INDEX_TABLE{
    mnos::FLAG_ID_CF_BIT_INDEX,
    mnos::FLAG_ID_ZF_BIT_INDEX,
    mnos::FLAG_ID_SF_BIT_INDEX,
    mnos::FLAG_ID_OF_BIT_INDEX
};
constexpr std::array<std::string_view, mnos::FLAG_ID_STATUS_FLAG_COUNT> FLAG_ID_ASSEMBLY_NAME_TABLE{
    "CF",
    "ZF",
    "SF",
    "OF"
};

[[nodiscard]] constexpr std::size_t to_index(mnos::FlagId id) noexcept
{
    return static_cast<std::size_t>(id);
}
}

namespace mnos
{
bool is_flag_id_valid(const FlagId id) noexcept
{
    return to_index(id) < FLAG_ID_STATUS_FLAG_COUNT;
}

std::size_t flag_id_to_index(const FlagId id)
{
    return to_index(id);
}

std::size_t flag_id_to_bit_index(const FlagId id)
{
    if (!is_flag_id_valid(id))
    {
        throw std::out_of_range{FLAG_ID_ACCESS_INVALID_ID_MESSAGE};
    }
    return FLAG_ID_BIT_INDEX_TABLE[to_index(id)];
}

std::string_view flag_id_to_assembly_name(const FlagId id) noexcept
{
    if (!is_flag_id_valid(id))
    {
        return FLAG_ID_ACCESS_INVALID_ID_MESSAGE;
    }
    return FLAG_ID_ASSEMBLY_NAME_TABLE[to_index(id)];
}
}
