#include <cstddef>
#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/memory/cache.hpp>
#include <support/deterministic_prng.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;

namespace
{
using ::testing::Le;

constexpr std::uint64_t CHAOS_STAGE8_CACHE_SEED = std::uint64_t{0xCACE'2026ULL};
constexpr std::size_t CHAOS_OPERATION_COUNT = 512;
constexpr std::size_t CHAOS_ADDRESS_SPACE_BYTES = 4096;
constexpr std::size_t CHAOS_MAX_ACCESS_BYTES = 96;
constexpr std::size_t CHAOS_LINE_SIZE_BYTES = 32;
constexpr std::size_t CHAOS_SET_COUNT = 8;
constexpr std::size_t CHAOS_ASSOCIATIVITY = 2;
constexpr std::size_t CHAOS_ACCESS_KIND_COUNT = 3;

[[nodiscard]] cpu_memory::CacheAccessKind access_kind_from_index(const std::size_t index) noexcept
{
    if (index == std::size_t{0})
    {
        return cpu_memory::CacheAccessKind::READ;
    }
    if (index == std::size_t{1})
    {
        return cpu_memory::CacheAccessKind::WRITE;
    }
    return cpu_memory::CacheAccessKind::EXECUTE;
}
}

TEST(Stage8CacheChaosTest, RandomAccessesPreserveAccountingAndCapacityInvariants)
{
    mnos::test::DeterministicPrng prng{CHAOS_STAGE8_CACHE_SEED};
    cpu_memory::SetAssociativeCache cache{
        cpu_memory::CacheGeometry{CHAOS_LINE_SIZE_BYTES, CHAOS_SET_COUNT, CHAOS_ASSOCIATIVITY}};

    std::size_t total_touched_lines = 0;
    std::size_t total_hits = 0;
    std::size_t total_misses = 0;
    std::size_t total_dirty_evictions = 0;
    for (std::size_t operation_index = 0; operation_index < CHAOS_OPERATION_COUNT; ++operation_index)
    {
        const cpu::Address64 address =
            static_cast<cpu::Address64>(prng.next_bounded(CHAOS_ADDRESS_SPACE_BYTES));
        const std::size_t byte_count =
            std::size_t{1} + prng.next_bounded(CHAOS_MAX_ACCESS_BYTES);
        const cpu_memory::CacheAccessKind kind =
            access_kind_from_index(prng.next_bounded(CHAOS_ACCESS_KIND_COUNT));

        const cpu_memory::CacheAccessResult result = cache.access(address, byte_count, kind);
        total_touched_lines += result.touched_line_count();
        total_hits += result.hit_count();
        total_misses += result.miss_count();
        total_dirty_evictions += result.dirty_eviction_count();

        EXPECT_THAT(cache.valid_line_count(), Le(cache.geometry().line_count()));
        EXPECT_THAT(result.dirty_eviction_count(), Le(result.miss_count()));
    }

    EXPECT_EQ(total_touched_lines, total_hits + total_misses);
    EXPECT_THAT(total_dirty_evictions, Le(total_misses));
    EXPECT_FALSE(cache.empty());
}
