#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace
{
constexpr std::string_view PRIVILEGE_LEVEL_INVALID_NAME = "<invalid>";
constexpr const char* PRIVILEGE_LEVEL_INVALID_ACCESS_MESSAGE = "privilege level is invalid";

class PrivilegeLevelCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::system::PrivilegeLevel level) noexcept
    {
        return RINGS.contains(level);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::system::PrivilegeLevel level) noexcept
    {
        return RINGS.index(level);
    }

    [[nodiscard]] static mnos::cpu::system::RingNumber ring(const mnos::cpu::system::PrivilegeLevel level)
    {
        return RING_NUMBERS.at(level, PRIVILEGE_LEVEL_INVALID_ACCESS_MESSAGE);
    }

    [[nodiscard]] static std::string_view name(const mnos::cpu::system::PrivilegeLevel level) noexcept
    {
        return RINGS.name(level);
    }

private:
    inline static constexpr auto RING_NUMBERS =
        mnos::core::make_enum_map<mnos::cpu::system::PrivilegeLevel>(
            std::array<mnos::cpu::system::RingNumber, mnos::cpu::system::PRIVILEGE_LEVEL_COUNT>{
                mnos::cpu::system::PRIVILEGE_LEVEL_RING0_NUMBER,
                mnos::cpu::system::PRIVILEGE_LEVEL_RING1_NUMBER,
                mnos::cpu::system::PRIVILEGE_LEVEL_RING2_NUMBER,
                mnos::cpu::system::PRIVILEGE_LEVEL_RING3_NUMBER});

    inline static constexpr auto RINGS = mnos::core::make_enum_name_table<mnos::cpu::system::PrivilegeLevel>(
        std::array<std::string_view, mnos::cpu::system::PRIVILEGE_LEVEL_COUNT>{"RING0", "RING1", "RING2", "RING3"},
        PRIVILEGE_LEVEL_INVALID_NAME);
};
}

namespace mnos::cpu::system
{
bool is_privilege_level_valid(const PrivilegeLevel level) noexcept
{
    return PrivilegeLevelCatalog::contains(level);
}

std::size_t privilege_level_to_index(const PrivilegeLevel level) noexcept
{
    return PrivilegeLevelCatalog::index(level);
}

RingNumber privilege_level_to_ring(const PrivilegeLevel level)
{
    return PrivilegeLevelCatalog::ring(level);
}

std::string_view privilege_level_to_name(const PrivilegeLevel level) noexcept
{
    return PrivilegeLevelCatalog::name(level);
}

bool is_more_privileged(const PrivilegeLevel target, const PrivilegeLevel current) noexcept
{
    return static_cast<std::uint8_t>(target) < static_cast<std::uint8_t>(current);
}
}
