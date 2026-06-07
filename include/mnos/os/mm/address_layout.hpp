#pragma once

#include <mnos/os/mm/address.hpp>
#include <mnos/os/mm/page.hpp>

namespace mnos::os::mm
{
inline constexpr VirtualAddress ADDRESS_LAYOUT_USER_LOW_BASE{AddressValue{0x1000}};
inline constexpr VirtualAddress ADDRESS_LAYOUT_USER_TEXT_BASE{AddressValue{0x400000}};
inline constexpr VirtualAddress ADDRESS_LAYOUT_USER_HEAP_BASE{AddressValue{0x40000000}};
inline constexpr VirtualAddress ADDRESS_LAYOUT_USER_STACK_TOP{AddressValue{0x00007000'00000000ULL}};
inline constexpr AddressValue ADDRESS_LAYOUT_USER_STACK_DEFAULT_SIZE_BYTES = MM_PAGE_SIZE_BYTES * AddressValue{16};
inline constexpr VirtualAddress ADDRESS_LAYOUT_KERNEL_HIGH_BASE{AddressValue{0xFFFF8000'00000000ULL}};

[[nodiscard]] bool is_user_address(VirtualAddress address) noexcept;
[[nodiscard]] bool is_user_range(VirtualAddress start_address, AddressValue byte_count) noexcept;
[[nodiscard]] bool is_kernel_address(VirtualAddress address) noexcept;
[[nodiscard]] bool is_kernel_range(VirtualAddress start_address, AddressValue byte_count) noexcept;
[[nodiscard]] VirtualAddress user_stack_bottom(AddressValue stack_size_bytes);
}
