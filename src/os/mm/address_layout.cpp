#include <limits>
#include <stdexcept>

#include <mnos/cpu/memory/paging.hpp>
#include <mnos/os/mm/address_layout.hpp>

namespace
{
constexpr const char* ADDRESS_LAYOUT_EMPTY_RANGE_MESSAGE = "address layout range must not be empty";
constexpr const char* ADDRESS_LAYOUT_STACK_SIZE_MESSAGE = "address layout user stack size is invalid";

[[nodiscard]] bool range_end_is_valid(
    const mnos::os::mm::VirtualAddress start_address,
    const mnos::os::mm::AddressValue byte_count,
    mnos::os::mm::VirtualAddress& last_address) noexcept
{
    if (byte_count == mnos::os::mm::AddressValue{0})
    {
        return false;
    }

    if (start_address.value() >
        std::numeric_limits<mnos::os::mm::AddressValue>::max() - (byte_count - mnos::os::mm::AddressValue{1}))
    {
        return false;
    }

    last_address = mnos::os::mm::VirtualAddress{start_address.value() + byte_count - mnos::os::mm::AddressValue{1}};
    return true;
}
}

namespace mnos::os::mm
{
bool is_user_address(const VirtualAddress address) noexcept
{
    return address >= ADDRESS_LAYOUT_USER_LOW_BASE &&
        address < ADDRESS_LAYOUT_USER_STACK_TOP &&
        cpu::memory::is_canonical_linear_address(to_cpu_address(address));
}

bool is_user_range(const VirtualAddress start_address, const AddressValue byte_count) noexcept
{
    VirtualAddress last_address;
    return range_end_is_valid(start_address, byte_count, last_address) &&
        is_user_address(start_address) &&
        is_user_address(last_address);
}

bool is_kernel_address(const VirtualAddress address) noexcept
{
    return address >= ADDRESS_LAYOUT_KERNEL_HIGH_BASE &&
        cpu::memory::is_canonical_linear_address(to_cpu_address(address));
}

bool is_kernel_range(const VirtualAddress start_address, const AddressValue byte_count) noexcept
{
    VirtualAddress last_address;
    return range_end_is_valid(start_address, byte_count, last_address) &&
        is_kernel_address(start_address) &&
        is_kernel_address(last_address);
}

VirtualAddress user_stack_bottom(const AddressValue stack_size_bytes)
{
    if (stack_size_bytes == AddressValue{0})
    {
        throw std::invalid_argument{ADDRESS_LAYOUT_EMPTY_RANGE_MESSAGE};
    }
    if (!is_page_aligned(stack_size_bytes) || stack_size_bytes > ADDRESS_LAYOUT_USER_STACK_TOP - ADDRESS_LAYOUT_USER_LOW_BASE)
    {
        throw std::out_of_range{ADDRESS_LAYOUT_STACK_SIZE_MESSAGE};
    }
    return ADDRESS_LAYOUT_USER_STACK_TOP - stack_size_bytes;
}
}
