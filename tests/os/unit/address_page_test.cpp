#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/address.hpp>
#include <mnos/os/mm/page.hpp>

namespace mm = mnos::os::mm;

namespace
{
using ::testing::Eq;

constexpr mm::AddressValue TEST_ZERO_ADDRESS_VALUE = mm::AddressValue{0};
constexpr mm::AddressValue TEST_BASE_ADDRESS_VALUE = mm::AddressValue{0x2000};
constexpr mm::AddressValue TEST_SECOND_ADDRESS_VALUE = mm::AddressValue{0x3000};
constexpr mm::AddressValue TEST_UNALIGNED_ADDRESS_VALUE = mm::AddressValue{0x2345};
constexpr mm::AddressValue TEST_UNALIGNED_ALIGN_DOWN_VALUE = mm::AddressValue{0x2000};
constexpr mm::AddressValue TEST_UNALIGNED_ALIGN_UP_VALUE = mm::AddressValue{0x3000};
constexpr mm::AddressValue TEST_UNALIGNED_OFFSET_VALUE = mm::AddressValue{0x345};
constexpr mm::PageNumber TEST_BASE_PAGE_NUMBER = mm::PageNumber{2};
constexpr mm::AddressValue TEST_TWO_PAGE_BYTE_COUNT = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr mm::AddressValue TEST_PARTIAL_THIRD_PAGE_BYTE_COUNT = TEST_TWO_PAGE_BYTE_COUNT + mm::AddressValue{1};
}

TEST(AddressTest, StrongTypesKeepPhysicalAndVirtualAddressesExplicit)
{
    mm::PhysicalAddress default_physical_address;
    mm::VirtualAddress default_virtual_address;
    mm::PhysicalAddress physical_address{TEST_BASE_ADDRESS_VALUE};
    mm::VirtualAddress virtual_address{TEST_BASE_ADDRESS_VALUE};

    EXPECT_TRUE(default_physical_address.is_zero());
    EXPECT_TRUE(default_virtual_address.is_zero());
    EXPECT_THAT(mm::PhysicalAddress::zero().value(), Eq(TEST_ZERO_ADDRESS_VALUE));
    EXPECT_THAT(physical_address.value(), Eq(TEST_BASE_ADDRESS_VALUE));
    EXPECT_THAT((physical_address + mm::MM_PAGE_SIZE_BYTES).value(), Eq(TEST_SECOND_ADDRESS_VALUE));
    EXPECT_THAT((mm::PhysicalAddress{TEST_SECOND_ADDRESS_VALUE} - mm::MM_PAGE_SIZE_BYTES).value(), Eq(TEST_BASE_ADDRESS_VALUE));
    EXPECT_THAT(mm::PhysicalAddress{TEST_SECOND_ADDRESS_VALUE} - physical_address, Eq(mm::MM_PAGE_SIZE_BYTES));
    EXPECT_TRUE(physical_address < mm::PhysicalAddress{TEST_SECOND_ADDRESS_VALUE});
    EXPECT_THAT(mm::to_cpu_address(physical_address), Eq(static_cast<mnos::cpu::Address64>(TEST_BASE_ADDRESS_VALUE)));
    EXPECT_THAT(virtual_address.value(), Eq(TEST_BASE_ADDRESS_VALUE));
    EXPECT_THAT((virtual_address + mm::MM_PAGE_SIZE_BYTES).value(), Eq(TEST_SECOND_ADDRESS_VALUE));
    EXPECT_THAT((mm::VirtualAddress{TEST_SECOND_ADDRESS_VALUE} - mm::MM_PAGE_SIZE_BYTES).value(), Eq(TEST_BASE_ADDRESS_VALUE));
    EXPECT_THAT(mm::VirtualAddress{TEST_SECOND_ADDRESS_VALUE} - virtual_address, Eq(mm::MM_PAGE_SIZE_BYTES));
    EXPECT_TRUE(virtual_address < mm::VirtualAddress{TEST_SECOND_ADDRESS_VALUE});
}

TEST(PageTest, AlignsAndClassifiesAddressValues)
{
    EXPECT_TRUE(mm::is_page_aligned(TEST_BASE_ADDRESS_VALUE));
    EXPECT_TRUE(mm::is_page_aligned(mm::PhysicalAddress{TEST_BASE_ADDRESS_VALUE}));
    EXPECT_TRUE(mm::is_page_aligned(mm::VirtualAddress{TEST_BASE_ADDRESS_VALUE}));
    EXPECT_FALSE(mm::is_page_aligned(TEST_UNALIGNED_ADDRESS_VALUE));

    EXPECT_THAT(mm::align_down_value(TEST_UNALIGNED_ADDRESS_VALUE), Eq(TEST_UNALIGNED_ALIGN_DOWN_VALUE));
    EXPECT_THAT(mm::align_up_value(TEST_UNALIGNED_ADDRESS_VALUE), Eq(TEST_UNALIGNED_ALIGN_UP_VALUE));
    EXPECT_THAT(mm::align_down(mm::PhysicalAddress{TEST_UNALIGNED_ADDRESS_VALUE}).value(), Eq(TEST_UNALIGNED_ALIGN_DOWN_VALUE));
    EXPECT_THAT(mm::align_up(mm::PhysicalAddress{TEST_UNALIGNED_ADDRESS_VALUE}).value(), Eq(TEST_UNALIGNED_ALIGN_UP_VALUE));
    EXPECT_THAT(mm::align_down(mm::VirtualAddress{TEST_UNALIGNED_ADDRESS_VALUE}).value(), Eq(TEST_UNALIGNED_ALIGN_DOWN_VALUE));
    EXPECT_THAT(mm::align_up(mm::VirtualAddress{TEST_UNALIGNED_ADDRESS_VALUE}).value(), Eq(TEST_UNALIGNED_ALIGN_UP_VALUE));
}

TEST(PageTest, ComputesPageNumbersOffsetsAndPageCounts)
{
    EXPECT_THAT(mm::page_number(TEST_BASE_ADDRESS_VALUE), Eq(TEST_BASE_PAGE_NUMBER));
    EXPECT_THAT(mm::page_number(mm::PhysicalAddress{TEST_BASE_ADDRESS_VALUE}), Eq(TEST_BASE_PAGE_NUMBER));
    EXPECT_THAT(mm::page_number(mm::VirtualAddress{TEST_BASE_ADDRESS_VALUE}), Eq(TEST_BASE_PAGE_NUMBER));
    EXPECT_THAT(mm::offset_in_page(TEST_UNALIGNED_ADDRESS_VALUE), Eq(TEST_UNALIGNED_OFFSET_VALUE));
    EXPECT_THAT(mm::offset_in_page(mm::PhysicalAddress{TEST_UNALIGNED_ADDRESS_VALUE}), Eq(TEST_UNALIGNED_OFFSET_VALUE));
    EXPECT_THAT(mm::offset_in_page(mm::VirtualAddress{TEST_UNALIGNED_ADDRESS_VALUE}), Eq(TEST_UNALIGNED_OFFSET_VALUE));
    EXPECT_THAT(mm::page_count_for_bytes(mm::AddressValue{0}), Eq(mm::PageNumber{0}));
    EXPECT_THAT(mm::page_count_for_bytes(TEST_TWO_PAGE_BYTE_COUNT), Eq(mm::PageNumber{2}));
    EXPECT_THAT(mm::page_count_for_bytes(TEST_PARTIAL_THIRD_PAGE_BYTE_COUNT), Eq(mm::PageNumber{3}));
}

TEST(PageTest, RejectsOverflowingRoundUpOperations)
{
    constexpr mm::AddressValue OVERFLOW_VALUE = std::numeric_limits<mm::AddressValue>::max();

    EXPECT_THROW(static_cast<void>(mm::align_up_value(OVERFLOW_VALUE)), std::overflow_error);
    EXPECT_THROW(static_cast<void>(mm::align_up(mm::PhysicalAddress{OVERFLOW_VALUE})), std::overflow_error);
    EXPECT_THROW(static_cast<void>(mm::align_up(mm::VirtualAddress{OVERFLOW_VALUE})), std::overflow_error);
    EXPECT_THROW(static_cast<void>(mm::page_count_for_bytes(OVERFLOW_VALUE)), std::overflow_error);
}
