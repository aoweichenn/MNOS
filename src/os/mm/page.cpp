#include <limits>
#include <stdexcept>

#include <mnos/os/mm/page.hpp>

namespace
{
constexpr const char* MM_PAGE_ALIGN_UP_OVERFLOW_MESSAGE = "page align-up overflows address value";
constexpr const char* MM_PAGE_COUNT_OVERFLOW_MESSAGE = "page count overflows address value";

[[nodiscard]] bool would_overflow_page_rounding(const mnos::os::mm::AddressValue value) noexcept
{
    return value > std::numeric_limits<mnos::os::mm::AddressValue>::max() - mnos::os::mm::MM_PAGE_OFFSET_MASK;
}
}

namespace mnos::os::mm
{
bool is_page_aligned(const AddressValue value) noexcept
{
    return (value & MM_PAGE_OFFSET_MASK) == AddressValue{0};
}

bool is_page_aligned(const PhysicalAddress address) noexcept
{
    return is_page_aligned(address.value());
}

bool is_page_aligned(const VirtualAddress address) noexcept
{
    return is_page_aligned(address.value());
}

AddressValue align_down_value(const AddressValue value) noexcept
{
    return value & MM_PAGE_FRAME_MASK;
}

AddressValue align_up_value(const AddressValue value)
{
    if (is_page_aligned(value))
    {
        return value;
    }

    if (would_overflow_page_rounding(value))
    {
        throw std::overflow_error{MM_PAGE_ALIGN_UP_OVERFLOW_MESSAGE};
    }

    return align_down_value(value + MM_PAGE_OFFSET_MASK);
}

PhysicalAddress align_down(const PhysicalAddress address) noexcept
{
    return PhysicalAddress{align_down_value(address.value())};
}

PhysicalAddress align_up(const PhysicalAddress address)
{
    return PhysicalAddress{align_up_value(address.value())};
}

VirtualAddress align_down(const VirtualAddress address) noexcept
{
    return VirtualAddress{align_down_value(address.value())};
}

VirtualAddress align_up(const VirtualAddress address)
{
    return VirtualAddress{align_up_value(address.value())};
}

PageNumber page_number(const AddressValue value) noexcept
{
    return value >> MM_PAGE_OFFSET_BITS;
}

PageNumber page_number(const PhysicalAddress address) noexcept
{
    return page_number(address.value());
}

PageNumber page_number(const VirtualAddress address) noexcept
{
    return page_number(address.value());
}

AddressValue offset_in_page(const AddressValue value) noexcept
{
    return value & MM_PAGE_OFFSET_MASK;
}

AddressValue offset_in_page(const PhysicalAddress address) noexcept
{
    return offset_in_page(address.value());
}

AddressValue offset_in_page(const VirtualAddress address) noexcept
{
    return offset_in_page(address.value());
}

PageNumber page_count_for_bytes(const AddressValue byte_count)
{
    if (byte_count == AddressValue{0})
    {
        return PageNumber{0};
    }

    if (would_overflow_page_rounding(byte_count))
    {
        throw std::overflow_error{MM_PAGE_COUNT_OVERFLOW_MESSAGE};
    }

    return page_number(byte_count + MM_PAGE_OFFSET_MASK);
}
}
