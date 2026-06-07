#pragma once

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace mnos::cpu::memory
{
class PageTableWalker final
{
public:
    [[nodiscard]] PageTranslation translate(
        MemoryBus& memory_bus,
        const PagingState& paging_state,
        Address64 linear_address,
        MemoryAccessKind access_kind,
        system::PrivilegeLevel privilege_level) const;
};
}
