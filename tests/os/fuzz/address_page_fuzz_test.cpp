#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/page.hpp>
#include <support/deterministic_prng.hpp>

namespace mm = mnos::os::mm;
namespace test = mnos::test;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t FUZZ_SEED = 0x0A11CE55ULL;
constexpr std::size_t FUZZ_PAGE_CASE_COUNT = 256;
constexpr std::size_t FUZZ_OVERFLOW_CASE_COUNT = 64;
constexpr std::uint64_t FUZZ_VALUE_LIMIT = std::uint64_t{1} << std::uint64_t{32};
constexpr mm::AddressValue FUZZ_EXPECTED_PAGE_OFFSET_MASK = mm::MM_PAGE_OFFSET_MASK;
constexpr mm::AddressValue FUZZ_EXPECTED_PAGE_FRAME_MASK = mm::MM_PAGE_FRAME_MASK;

[[nodiscard]] mm::AddressValue expected_align_up(const mm::AddressValue value) noexcept
{
    if ((value & FUZZ_EXPECTED_PAGE_OFFSET_MASK) == mm::AddressValue{0})
    {
        return value;
    }
    return (value + FUZZ_EXPECTED_PAGE_OFFSET_MASK) & FUZZ_EXPECTED_PAGE_FRAME_MASK;
}
}

TEST(AddressPageFuzzTest, PageOperationsMatchBitModelForRandomValues)
{
    test::DeterministicPrng prng{FUZZ_SEED};

    for (std::size_t case_index = 0; case_index < FUZZ_PAGE_CASE_COUNT; ++case_index)
    {
        const mm::AddressValue value = static_cast<mm::AddressValue>(prng.next_bounded(FUZZ_VALUE_LIMIT));
        const mm::AddressValue expected_down = value & FUZZ_EXPECTED_PAGE_FRAME_MASK;
        const mm::AddressValue expected_offset = value & FUZZ_EXPECTED_PAGE_OFFSET_MASK;
        const mm::PageNumber expected_page_number = value >> mm::MM_PAGE_OFFSET_BITS;

        EXPECT_THAT(mm::align_down_value(value), Eq(expected_down));
        EXPECT_THAT(mm::align_up_value(value), Eq(expected_align_up(value)));
        EXPECT_THAT(mm::page_number(value), Eq(expected_page_number));
        EXPECT_THAT(mm::offset_in_page(value), Eq(expected_offset));
        EXPECT_THAT(mm::align_down(mm::PhysicalAddress{value}).value(), Eq(expected_down));
        EXPECT_THAT(mm::align_up(mm::VirtualAddress{value}).value(), Eq(expected_align_up(value)));
    }
}

TEST(AddressPageFuzzTest, NearMaxUnalignedValuesRejectOverflowingRoundUp)
{
    test::DeterministicPrng prng{FUZZ_SEED ^ std::uint64_t{0xFACEFEEDULL}};
    constexpr mm::AddressValue MAX_VALUE = std::numeric_limits<mm::AddressValue>::max();

    for (std::size_t case_index = 0; case_index < FUZZ_OVERFLOW_CASE_COUNT; ++case_index)
    {
        const mm::AddressValue value =
            MAX_VALUE - static_cast<mm::AddressValue>(prng.next_bounded(mm::MM_PAGE_OFFSET_MASK));
        ASSERT_FALSE(mm::is_page_aligned(value));
        EXPECT_THROW(static_cast<void>(mm::align_up_value(value)), std::overflow_error);
        EXPECT_THROW(static_cast<void>(mm::page_count_for_bytes(value)), std::overflow_error);
    }
}
