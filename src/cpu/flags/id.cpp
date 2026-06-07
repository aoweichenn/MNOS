#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/flags/id.hpp>

namespace
{
constexpr std::string_view FLAG_ID_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr const char* FLAG_ID_ACCESS_INVALID_ID_MESSAGE = "flag id is invalid";

class FlagIdCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::FlagId id) noexcept
    {
        return FLAG_ID_NAMES.contains(id);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::FlagId id) noexcept
    {
        return FLAG_ID_NAMES.index(id);
    }

    [[nodiscard]] static std::size_t bit_index(const mnos::cpu::FlagId id)
    {
        return FLAG_ID_BIT_INDICES.at(id, FLAG_ID_ACCESS_INVALID_ID_MESSAGE);
    }

    [[nodiscard]] static std::string_view assembly_name(const mnos::cpu::FlagId id) noexcept
    {
        return FLAG_ID_NAMES.name(id);
    }

private:
    inline static constexpr auto FLAG_ID_BIT_INDICES = mnos::core::make_enum_map<mnos::cpu::FlagId>(
        std::array<std::size_t, mnos::cpu::FLAG_ID_STATUS_FLAG_COUNT>{
            mnos::cpu::FLAG_ID_CF_BIT_INDEX,
            mnos::cpu::FLAG_ID_ZF_BIT_INDEX,
            mnos::cpu::FLAG_ID_SF_BIT_INDEX,
            mnos::cpu::FLAG_ID_OF_BIT_INDEX});

    inline static constexpr auto FLAG_ID_NAMES = mnos::core::make_enum_name_table<mnos::cpu::FlagId>(
        std::array<std::string_view, mnos::cpu::FLAG_ID_STATUS_FLAG_COUNT>{"CF", "ZF", "SF", "OF"},
        FLAG_ID_ASSEMBLY_NAME_INVALID_TEXT);
};
}

namespace mnos::cpu
{
bool is_flag_id_valid(const FlagId id) noexcept
{
    return FlagIdCatalog::contains(id);
}

std::size_t flag_id_to_index(const FlagId id) noexcept
{
    return FlagIdCatalog::index(id);
}

std::size_t flag_id_to_bit_index(const FlagId id)
{
    return FlagIdCatalog::bit_index(id);
}

std::string_view flag_id_to_assembly_name(const FlagId id) noexcept
{
    return FlagIdCatalog::assembly_name(id);
}
}
