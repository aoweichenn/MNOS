#pragma once

#include <cstdint>

#include <mnos/os/mm/address.hpp>

namespace mnos::os::mm
{
using PageNumber = std::uint64_t;

inline constexpr AddressValue MM_PAGE_OFFSET_BITS = AddressValue{12};
inline constexpr AddressValue MM_PAGE_SIZE_BYTES = AddressValue{1} << MM_PAGE_OFFSET_BITS;
inline constexpr AddressValue MM_PAGE_OFFSET_MASK = MM_PAGE_SIZE_BYTES - AddressValue{1};
inline constexpr AddressValue MM_PAGE_FRAME_MASK = ~MM_PAGE_OFFSET_MASK;

[[nodiscard]] bool is_page_aligned(AddressValue value) noexcept;
[[nodiscard]] bool is_page_aligned(PhysicalAddress address) noexcept;
[[nodiscard]] bool is_page_aligned(VirtualAddress address) noexcept;

[[nodiscard]] AddressValue align_down_value(AddressValue value) noexcept;
[[nodiscard]] AddressValue align_up_value(AddressValue value);

[[nodiscard]] PhysicalAddress align_down(PhysicalAddress address) noexcept;
[[nodiscard]] PhysicalAddress align_up(PhysicalAddress address);
[[nodiscard]] VirtualAddress align_down(VirtualAddress address) noexcept;
[[nodiscard]] VirtualAddress align_up(VirtualAddress address);

[[nodiscard]] PageNumber page_number(AddressValue value) noexcept;
[[nodiscard]] PageNumber page_number(PhysicalAddress address) noexcept;
[[nodiscard]] PageNumber page_number(VirtualAddress address) noexcept;

[[nodiscard]] AddressValue offset_in_page(AddressValue value) noexcept;
[[nodiscard]] AddressValue offset_in_page(PhysicalAddress address) noexcept;
[[nodiscard]] AddressValue offset_in_page(VirtualAddress address) noexcept;

[[nodiscard]] PageNumber page_count_for_bytes(AddressValue byte_count);
}
