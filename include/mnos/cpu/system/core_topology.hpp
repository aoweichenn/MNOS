#pragma once

#include <cstdint>

#include <mnos/cpu/system/core_id.hpp>

namespace mnos::cpu::system
{
inline constexpr std::uint32_t CORE_TOPOLOGY_DEFAULT_CORE_COUNT = std::uint32_t{1};

class CoreTopology final
{
public:
    CoreTopology();
    explicit CoreTopology(std::uint32_t core_count);

    [[nodiscard]] static CoreTopology single_core();

    [[nodiscard]] std::uint32_t core_count() const noexcept;
    [[nodiscard]] bool contains(CoreId core_id) const noexcept;
    [[nodiscard]] CoreId bootstrap_core() const noexcept;
    [[nodiscard]] CoreId core_at(std::uint32_t index) const;

private:
    std::uint32_t core_count_ = CORE_TOPOLOGY_DEFAULT_CORE_COUNT;
};
}
