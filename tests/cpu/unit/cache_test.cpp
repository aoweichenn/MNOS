#include <cstddef>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/memory/cache.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_LINE_SIZE_BYTES = 16;
constexpr std::size_t TEST_SET_COUNT = 2;
constexpr std::size_t TEST_ASSOCIATIVITY = 2;
constexpr std::size_t TEST_LINE_SIZE_SHIFT = 4;
constexpr std::size_t TEST_SET_COUNT_SHIFT = 1;
constexpr std::size_t TEST_SET_INDEX_MASK = 1;
constexpr cpu::Address64 TEST_BASE_ADDRESS = cpu::Address64{0x40};
constexpr cpu::Address64 TEST_SAME_LINE_ADDRESS = TEST_BASE_ADDRESS + cpu::Address64{8};
constexpr cpu::Address64 TEST_COLLIDING_ADDRESS_A = cpu::Address64{0x00};
constexpr cpu::Address64 TEST_COLLIDING_ADDRESS_B = cpu::Address64{0x20};
constexpr cpu::Address64 TEST_COLLIDING_ADDRESS_C = cpu::Address64{0x40};
constexpr std::size_t TEST_QWORD_BYTES = 8;
constexpr std::size_t TEST_CROSS_LINE_BYTES = 24;
}

TEST(CacheGeometryTest, ComputesCapacityAndRejectsInvalidShapes)
{
    const cpu_memory::CacheGeometry geometry{TEST_LINE_SIZE_BYTES, TEST_SET_COUNT, TEST_ASSOCIATIVITY};

    EXPECT_THAT(geometry.line_size_bytes(), Eq(TEST_LINE_SIZE_BYTES));
    EXPECT_THAT(geometry.set_count(), Eq(TEST_SET_COUNT));
    EXPECT_THAT(geometry.associativity(), Eq(TEST_ASSOCIATIVITY));
    EXPECT_THAT(geometry.line_count(), Eq(TEST_SET_COUNT * TEST_ASSOCIATIVITY));
    EXPECT_THAT(geometry.capacity_bytes(), Eq(TEST_LINE_SIZE_BYTES * TEST_SET_COUNT * TEST_ASSOCIATIVITY));
    EXPECT_THAT(geometry.line_size_shift(), Eq(TEST_LINE_SIZE_SHIFT));
    EXPECT_THAT(geometry.set_count_shift(), Eq(TEST_SET_COUNT_SHIFT));
    EXPECT_THAT(geometry.set_index_mask(), Eq(TEST_SET_INDEX_MASK));

    EXPECT_THROW(static_cast<void>(cpu_memory::CacheGeometry{0, TEST_SET_COUNT, TEST_ASSOCIATIVITY}), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(cpu_memory::CacheGeometry{TEST_LINE_SIZE_BYTES, 3, TEST_ASSOCIATIVITY}), std::invalid_argument);
}

TEST(SetAssociativeCacheTest, RecordsHitsMissesAndCrossLineAccess)
{
    cpu_memory::SetAssociativeCache cache{
        cpu_memory::CacheGeometry{TEST_LINE_SIZE_BYTES, TEST_SET_COUNT, TEST_ASSOCIATIVITY}};

    EXPECT_TRUE(cache.empty());
    cpu_memory::CacheAccessResult result =
        cache.access(TEST_BASE_ADDRESS, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::READ);
    EXPECT_THAT(result.hit_count(), Eq(std::size_t{0}));
    EXPECT_THAT(result.miss_count(), Eq(std::size_t{1}));
    EXPECT_TRUE(result.any_miss());
    EXPECT_THAT(cache.valid_line_count(), Eq(std::size_t{1}));

    result = cache.access(TEST_SAME_LINE_ADDRESS, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::READ);
    EXPECT_THAT(result.hit_count(), Eq(std::size_t{1}));
    EXPECT_THAT(result.miss_count(), Eq(std::size_t{0}));
    EXPECT_TRUE(result.all_hits());

    result = cache.access(TEST_BASE_ADDRESS, TEST_CROSS_LINE_BYTES, cpu_memory::CacheAccessKind::EXECUTE);
    EXPECT_THAT(result.touched_line_count(), Eq(std::size_t{2}));
    EXPECT_THAT(result.hit_count(), Eq(std::size_t{1}));
    EXPECT_THAT(result.miss_count(), Eq(std::size_t{1}));
    EXPECT_THAT(cache.valid_line_count(), Eq(std::size_t{2}));
}

TEST(SetAssociativeCacheTest, EvictsLeastRecentlyUsedDirtyLine)
{
    cpu_memory::SetAssociativeCache cache{
        cpu_memory::CacheGeometry{TEST_LINE_SIZE_BYTES, std::size_t{1}, TEST_ASSOCIATIVITY},
        cpu_memory::CacheWritePolicy::WRITE_BACK};

    static_cast<void>(cache.access(TEST_COLLIDING_ADDRESS_A, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::WRITE));
    static_cast<void>(cache.access(TEST_COLLIDING_ADDRESS_B, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::READ));
    static_cast<void>(cache.access(TEST_COLLIDING_ADDRESS_B, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::READ));

    const cpu_memory::CacheAccessResult result =
        cache.access(TEST_COLLIDING_ADDRESS_C, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::READ);

    EXPECT_THAT(result.miss_count(), Eq(std::size_t{1}));
    EXPECT_THAT(result.dirty_eviction_count(), Eq(std::size_t{1}));
    EXPECT_THAT(cache.valid_line_count(), Eq(TEST_ASSOCIATIVITY));
}

TEST(SetAssociativeCacheTest, WriteThroughDoesNotDirtyOrEvictDirtyLines)
{
    cpu_memory::SetAssociativeCache cache{
        cpu_memory::CacheGeometry{TEST_LINE_SIZE_BYTES, std::size_t{1}, TEST_ASSOCIATIVITY},
        cpu_memory::CacheWritePolicy::WRITE_THROUGH};

    static_cast<void>(cache.access(TEST_COLLIDING_ADDRESS_A, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::WRITE));
    static_cast<void>(cache.access(TEST_COLLIDING_ADDRESS_B, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::WRITE));
    const cpu_memory::CacheAccessResult result =
        cache.access(TEST_COLLIDING_ADDRESS_C, TEST_QWORD_BYTES, cpu_memory::CacheAccessKind::WRITE);

    EXPECT_THAT(result.miss_count(), Eq(std::size_t{1}));
    EXPECT_THAT(result.dirty_eviction_count(), Eq(std::size_t{0}));

    cache.clear();
    EXPECT_TRUE(cache.empty());
    EXPECT_THAT(cache.valid_line_count(), Eq(std::size_t{0}));
}
