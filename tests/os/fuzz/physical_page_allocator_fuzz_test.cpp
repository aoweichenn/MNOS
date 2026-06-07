#include <array>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/page.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <support/deterministic_prng.hpp>

namespace mm = mnos::os::mm;
namespace test = mnos::test;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t FUZZ_SEED = 0x9E3779B97F4A7C15ULL;
constexpr mm::PageNumber FUZZ_TOTAL_PAGE_COUNT = mm::PageNumber{32};
constexpr mm::PageNumber FUZZ_FIRST_ALLOCATABLE_PAGE = mm::PageNumber{2};
constexpr std::size_t FUZZ_OPERATION_COUNT = 256;

[[nodiscard]] mm::PhysicalAddress address_for_page(const mm::PageNumber page_index) noexcept
{
    return mm::PhysicalAddress{page_index * mm::MM_PAGE_SIZE_BYTES};
}
}

TEST(PhysicalPageAllocatorFuzzTest, RandomAllocateFreeMatchesBitmapModel)
{
    test::DeterministicPrng prng{FUZZ_SEED};
    mm::PhysicalPageAllocator allocator{FUZZ_TOTAL_PAGE_COUNT, FUZZ_FIRST_ALLOCATABLE_PAGE};
    std::array<bool, static_cast<std::size_t>(FUZZ_TOTAL_PAGE_COUNT)> allocated_pages{};
    for (mm::PageNumber page_index = mm::PageNumber{0}; page_index < FUZZ_FIRST_ALLOCATABLE_PAGE; ++page_index)
    {
        allocated_pages[static_cast<std::size_t>(page_index)] = true;
    }

    for (std::size_t operation_index = 0; operation_index < FUZZ_OPERATION_COUNT; ++operation_index)
    {
        if (prng.next_bounded(std::uint64_t{2}) == std::uint64_t{0} && !allocator.empty())
        {
            const mm::PhysicalAddress page = allocator.allocate_page();
            allocated_pages[static_cast<std::size_t>(mm::page_number(page))] = true;
        }
        else
        {
            const mm::PageNumber page_index =
                FUZZ_FIRST_ALLOCATABLE_PAGE + static_cast<mm::PageNumber>(
                    prng.next_bounded(FUZZ_TOTAL_PAGE_COUNT - FUZZ_FIRST_ALLOCATABLE_PAGE));
            if (allocated_pages[static_cast<std::size_t>(page_index)])
            {
                allocator.free_page(address_for_page(page_index));
                allocated_pages[static_cast<std::size_t>(page_index)] = false;
            }
        }

        mm::PageNumber expected_free_pages = mm::PageNumber{0};
        for (mm::PageNumber page_index = FUZZ_FIRST_ALLOCATABLE_PAGE; page_index < FUZZ_TOTAL_PAGE_COUNT; ++page_index)
        {
            const bool expected_allocated = allocated_pages[static_cast<std::size_t>(page_index)];
            EXPECT_THAT(allocator.is_allocated(address_for_page(page_index)), Eq(expected_allocated));
            if (!expected_allocated)
            {
                ++expected_free_pages;
            }
        }
        EXPECT_THAT(allocator.free_page_count(), Eq(expected_free_pages));
    }
}
