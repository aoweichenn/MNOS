#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/page.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>

namespace mm = mnos::os::mm;

namespace
{
using ::testing::Eq;

constexpr mm::PageNumber TEST_TOTAL_PAGE_COUNT = mm::PageNumber{8};
constexpr mm::PageNumber TEST_FIRST_ALLOCATABLE_PAGE = mm::PageNumber{2};
constexpr mm::PhysicalAddress TEST_UNALIGNED_ADDRESS{mm::MM_PAGE_SIZE_BYTES + mm::AddressValue{1}};
constexpr mm::PhysicalAddress TEST_OUT_OF_RANGE_ADDRESS{TEST_TOTAL_PAGE_COUNT * mm::MM_PAGE_SIZE_BYTES};
}

TEST(PhysicalPageAllocatorTest, AllocatesAndFreesSingleAndContiguousPages)
{
    mm::PhysicalPageAllocator allocator{TEST_TOTAL_PAGE_COUNT, TEST_FIRST_ALLOCATABLE_PAGE};

    EXPECT_THAT(allocator.total_page_count(), Eq(TEST_TOTAL_PAGE_COUNT));
    EXPECT_THAT(allocator.first_allocatable_page(), Eq(TEST_FIRST_ALLOCATABLE_PAGE));
    EXPECT_THAT(allocator.free_page_count(), Eq(mm::PageNumber{6}));
    EXPECT_THAT(allocator.allocated_page_count(), Eq(mm::PageNumber{2}));
    EXPECT_FALSE(allocator.empty());

    const mm::PhysicalAddress first_page = allocator.allocate_page();
    EXPECT_THAT(first_page.value(), Eq(TEST_FIRST_ALLOCATABLE_PAGE * mm::MM_PAGE_SIZE_BYTES));
    EXPECT_TRUE(allocator.is_allocated(first_page));
    EXPECT_THAT(allocator.free_page_count(), Eq(mm::PageNumber{5}));

    const mm::PhysicalAddress contiguous_pages = allocator.allocate_contiguous(mm::PageNumber{2});
    EXPECT_THAT(contiguous_pages.value(), Eq(mm::AddressValue{3} * mm::MM_PAGE_SIZE_BYTES));
    EXPECT_TRUE(allocator.is_allocated(contiguous_pages));
    EXPECT_TRUE(allocator.is_allocated(contiguous_pages + mm::MM_PAGE_SIZE_BYTES));

    allocator.free_page(first_page);
    EXPECT_FALSE(allocator.is_allocated(first_page));
    EXPECT_THAT(allocator.free_page_count(), Eq(mm::PageNumber{4}));
    EXPECT_THAT(allocator.allocate_page(), Eq(first_page));
}

TEST(PhysicalPageAllocatorTest, ReservesRangesAndRejectsInvalidOperations)
{
    mm::PhysicalPageAllocator allocator{TEST_TOTAL_PAGE_COUNT, TEST_FIRST_ALLOCATABLE_PAGE};

    allocator.reserve_range(mm::PhysicalAddress{mm::AddressValue{4} * mm::MM_PAGE_SIZE_BYTES}, mm::PageNumber{2});
    EXPECT_TRUE(allocator.is_allocated(mm::PhysicalAddress{mm::AddressValue{4} * mm::MM_PAGE_SIZE_BYTES}));
    EXPECT_TRUE(allocator.is_allocated(mm::PhysicalAddress{mm::AddressValue{5} * mm::MM_PAGE_SIZE_BYTES}));

    EXPECT_TRUE(allocator.contains(mm::PhysicalAddress{mm::AddressValue{7} * mm::MM_PAGE_SIZE_BYTES}));
    EXPECT_FALSE(allocator.contains(TEST_UNALIGNED_ADDRESS));
    EXPECT_FALSE(allocator.contains(TEST_OUT_OF_RANGE_ADDRESS));
    EXPECT_THROW(static_cast<void>(mm::PhysicalPageAllocator{mm::PageNumber{0}, mm::PageNumber{0}}), std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(mm::PhysicalPageAllocator{TEST_TOTAL_PAGE_COUNT, TEST_TOTAL_PAGE_COUNT + mm::PageNumber{1}}),
        std::out_of_range);
    EXPECT_THROW(static_cast<void>(allocator.allocate_contiguous(mm::PageNumber{0})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(allocator.is_allocated(TEST_UNALIGNED_ADDRESS)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(allocator.is_allocated(TEST_OUT_OF_RANGE_ADDRESS)), std::out_of_range);
    EXPECT_THROW(allocator.free_page(mm::PhysicalAddress::zero()), std::logic_error);
    EXPECT_THROW(allocator.free_page(mm::PhysicalAddress{mm::AddressValue{6} * mm::MM_PAGE_SIZE_BYTES}), std::logic_error);
    EXPECT_THROW(
        allocator.reserve_range(mm::PhysicalAddress{mm::AddressValue{7} * mm::MM_PAGE_SIZE_BYTES}, mm::PageNumber{2}),
        std::out_of_range);
}

TEST(PhysicalPageAllocatorTest, ReportsOutOfMemoryWhenNoContiguousRunExists)
{
    mm::PhysicalPageAllocator allocator{mm::PageNumber{5}, mm::PageNumber{1}};
    allocator.reserve_range(mm::PhysicalAddress{mm::AddressValue{2} * mm::MM_PAGE_SIZE_BYTES}, mm::PageNumber{1});
    allocator.reserve_range(mm::PhysicalAddress{mm::AddressValue{4} * mm::MM_PAGE_SIZE_BYTES}, mm::PageNumber{1});

    EXPECT_THROW(static_cast<void>(allocator.allocate_contiguous(mm::PageNumber{2})), std::bad_alloc);
}
