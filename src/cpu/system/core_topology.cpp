#include <stdexcept>

#include <mnos/cpu/system/core_topology.hpp>

namespace
{
constexpr const char* CORE_TOPOLOGY_ZERO_CORE_COUNT_MESSAGE = "core topology requires at least one core";
constexpr const char* CORE_TOPOLOGY_CORE_INDEX_OUT_OF_RANGE_MESSAGE = "core topology core index is out of range";
}

namespace mnos::cpu::system
{
CoreTopology::CoreTopology() : CoreTopology(CORE_TOPOLOGY_DEFAULT_CORE_COUNT)
{
}

CoreTopology::CoreTopology(const std::uint32_t core_count) : core_count_(core_count)
{
    if (this->core_count_ == std::uint32_t{0})
    {
        throw std::invalid_argument{CORE_TOPOLOGY_ZERO_CORE_COUNT_MESSAGE};
    }
}

CoreTopology CoreTopology::single_core()
{
    return CoreTopology{CORE_TOPOLOGY_DEFAULT_CORE_COUNT};
}

std::uint32_t CoreTopology::core_count() const noexcept
{
    return this->core_count_;
}

bool CoreTopology::contains(const CoreId core_id) const noexcept
{
    return core_id.value() < this->core_count_;
}

CoreId CoreTopology::bootstrap_core() const noexcept
{
    return CoreId::bootstrap();
}

CoreId CoreTopology::core_at(const std::uint32_t index) const
{
    if (index >= this->core_count_)
    {
        throw std::out_of_range{CORE_TOPOLOGY_CORE_INDEX_OUT_OF_RANGE_MESSAGE};
    }

    return CoreId{index};
}
}
