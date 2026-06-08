#include <cstddef>
#include <cstdint>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/block/block_device.hpp>
#include <mnos/os/block/buffer_cache.hpp>

namespace cpu = mnos::cpu;
namespace block = mnos::os::block;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_BLOCK_SIZE_BYTES = 16;
constexpr std::uint64_t TEST_BLOCK_COUNT = std::uint64_t{6};
constexpr std::size_t TEST_CACHE_CAPACITY_BLOCKS = 2;
constexpr block::BlockAddress TEST_BLOCK_0{std::uint64_t{0}};
constexpr block::BlockAddress TEST_BLOCK_1{std::uint64_t{1}};
constexpr block::BlockAddress TEST_BLOCK_2{std::uint64_t{2}};
constexpr cpu::Byte TEST_DATA_SEED_A = cpu::Byte{0x31};
constexpr cpu::Byte TEST_DATA_SEED_B = cpu::Byte{0x51};
constexpr cpu::Byte TEST_DATA_SEED_C = cpu::Byte{0x71};

[[nodiscard]] std::vector<cpu::Byte> make_block(const cpu::Byte seed)
{
    std::vector<cpu::Byte> bytes(TEST_BLOCK_SIZE_BYTES);
    for (std::size_t byte_index = std::size_t{0}; byte_index < bytes.size(); ++byte_index)
    {
        bytes[byte_index] = static_cast<cpu::Byte>(seed + static_cast<cpu::Byte>(byte_index));
    }
    return bytes;
}

void expect_cache_read(
    block::BufferCache& cache,
    const block::BlockAddress address,
    const std::vector<cpu::Byte>& expected)
{
    std::vector<cpu::Byte> actual(TEST_BLOCK_SIZE_BYTES);
    cache.read_block(address, actual);
    EXPECT_THAT(actual, Eq(expected));
}
}

TEST(Stage15BlockCacheIntegrationTest, DirtyBlocksSurviveEvictionFlushAndNewCacheInstance)
{
    block::MemoryBlockDevice device{block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, TEST_BLOCK_COUNT}};
    const std::vector<cpu::Byte> data_a = make_block(TEST_DATA_SEED_A);
    const std::vector<cpu::Byte> data_b = make_block(TEST_DATA_SEED_B);
    const std::vector<cpu::Byte> data_c = make_block(TEST_DATA_SEED_C);

    {
        block::BufferCache cache{device, TEST_CACHE_CAPACITY_BLOCKS};
        cache.write_block(TEST_BLOCK_0, data_a);
        cache.write_block(TEST_BLOCK_1, data_b);
        cache.write_block(TEST_BLOCK_2, data_c);

        EXPECT_FALSE(cache.contains(TEST_BLOCK_0));
        EXPECT_THAT(cache.stats().dirty_eviction_count(), Eq(std::uint64_t{1}));
        cache.flush_all();
    }

    block::BufferCache second_cache{device, TEST_CACHE_CAPACITY_BLOCKS};
    expect_cache_read(second_cache, TEST_BLOCK_0, data_a);
    expect_cache_read(second_cache, TEST_BLOCK_1, data_b);
    expect_cache_read(second_cache, TEST_BLOCK_2, data_c);
    EXPECT_THAT(second_cache.stats().device_read_count(), Eq(std::uint64_t{3}));
}
