#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
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

constexpr std::size_t TEST_BLOCK_SIZE_BYTES = 8;
constexpr std::uint64_t TEST_BLOCK_COUNT = std::uint64_t{4};
constexpr std::uint64_t TEST_CAPACITY_BYTES = std::uint64_t{32};
constexpr std::size_t TEST_CACHE_CAPACITY_BLOCKS = 2;
constexpr std::size_t TEST_MULTI_BLOCK_BYTE_COUNT = TEST_BLOCK_SIZE_BYTES * std::size_t{2};
constexpr block::BlockAddress TEST_BLOCK_0{std::uint64_t{0}};
constexpr block::BlockAddress TEST_BLOCK_1{std::uint64_t{1}};
constexpr block::BlockAddress TEST_BLOCK_2{std::uint64_t{2}};
constexpr block::BlockAddress TEST_BLOCK_3{std::uint64_t{3}};
constexpr block::BlockAddress TEST_BLOCK_END{TEST_BLOCK_COUNT};
constexpr block::BlockAddress TEST_BLOCK_AFTER_END{TEST_BLOCK_COUNT + std::uint64_t{1}};
constexpr cpu::Byte TEST_OLD_FILL_BYTE = cpu::Byte{0x11};
constexpr cpu::Byte TEST_DATA_SEED_A = cpu::Byte{0x20};
constexpr cpu::Byte TEST_DATA_SEED_B = cpu::Byte{0x40};
constexpr cpu::Byte TEST_DATA_SEED_C = cpu::Byte{0x60};
constexpr cpu::Byte TEST_CLEAR_FILL_BYTE = cpu::Byte{0x7E};

[[nodiscard]] std::vector<cpu::Byte> make_block(const cpu::Byte seed)
{
    std::vector<cpu::Byte> bytes(TEST_BLOCK_SIZE_BYTES);
    for (std::size_t byte_index = std::size_t{0}; byte_index < bytes.size(); ++byte_index)
    {
        bytes[byte_index] = static_cast<cpu::Byte>(seed + static_cast<cpu::Byte>(byte_index));
    }
    return bytes;
}

[[nodiscard]] std::vector<cpu::Byte> make_empty_io_buffer()
{
    return {};
}

[[nodiscard]] block::MemoryBlockDevice make_device()
{
    return block::MemoryBlockDevice{block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, TEST_BLOCK_COUNT}};
}

void expect_device_block(
    const block::MemoryBlockDevice& device,
    const block::BlockAddress address,
    const std::vector<cpu::Byte>& expected)
{
    std::vector<cpu::Byte> actual(TEST_BLOCK_SIZE_BYTES);
    device.read_block(address, actual);
    EXPECT_THAT(actual, Eq(expected));
}
}

TEST(Stage15BlockDeviceGeometryTest, ComputesCapacityAndRejectsInvalidGeometry)
{
    const block::BlockDeviceGeometry geometry{TEST_BLOCK_SIZE_BYTES, TEST_BLOCK_COUNT};

    EXPECT_THAT(geometry.block_size_bytes(), Eq(TEST_BLOCK_SIZE_BYTES));
    EXPECT_THAT(geometry.block_count(), Eq(TEST_BLOCK_COUNT));
    EXPECT_THAT(geometry.capacity_bytes(), Eq(TEST_CAPACITY_BYTES));

    EXPECT_THROW(static_cast<void>(block::BlockDeviceGeometry{std::size_t{0}, TEST_BLOCK_COUNT}), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, std::uint64_t{0}}), std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(block::BlockDeviceGeometry{std::size_t{2}, std::numeric_limits<std::uint64_t>::max()}),
        std::overflow_error);
}

TEST(Stage15MemoryBlockDeviceTest, ReadsWritesClearsAndReportsContainment)
{
    block::MemoryBlockDevice device = make_device();
    const std::vector<cpu::Byte> data = make_block(TEST_DATA_SEED_A);
    std::vector<cpu::Byte> destination(TEST_BLOCK_SIZE_BYTES);

    EXPECT_TRUE(device.contains(TEST_BLOCK_0));
    EXPECT_TRUE(device.contains(TEST_BLOCK_3));
    EXPECT_FALSE(device.contains(TEST_BLOCK_END));
    EXPECT_THAT(device.block_size_bytes(), Eq(TEST_BLOCK_SIZE_BYTES));
    EXPECT_THAT(device.block_count(), Eq(TEST_BLOCK_COUNT));
    EXPECT_THAT(device.capacity_bytes(), Eq(TEST_CAPACITY_BYTES));

    device.write_block(TEST_BLOCK_2, data);
    device.read_block(TEST_BLOCK_2, destination);
    EXPECT_THAT(destination, Eq(data));

    device.clear(TEST_CLEAR_FILL_BYTE);
    const std::vector<cpu::Byte> cleared(TEST_BLOCK_SIZE_BYTES, TEST_CLEAR_FILL_BYTE);
    device.read_block(TEST_BLOCK_2, destination);
    EXPECT_THAT(destination, Eq(cleared));
}

TEST(Stage15MemoryBlockDeviceTest, SupportsContiguousWholeBlockIo)
{
    block::MemoryBlockDevice device = make_device();
    const std::vector<cpu::Byte> block_a = make_block(TEST_DATA_SEED_A);
    const std::vector<cpu::Byte> block_b = make_block(TEST_DATA_SEED_B);
    std::vector<cpu::Byte> source;
    source.reserve(TEST_MULTI_BLOCK_BYTE_COUNT);
    source.insert(source.end(), block_a.begin(), block_a.end());
    source.insert(source.end(), block_b.begin(), block_b.end());

    std::vector<cpu::Byte> destination(TEST_MULTI_BLOCK_BYTE_COUNT);
    device.write_blocks(TEST_BLOCK_1, source);
    device.read_blocks(TEST_BLOCK_1, destination);

    EXPECT_THAT(destination, Eq(source));

    std::vector<cpu::Byte> empty = make_empty_io_buffer();
    device.read_blocks(TEST_BLOCK_END, empty);
    device.write_blocks(TEST_BLOCK_END, empty);
}

TEST(Stage15MemoryBlockDeviceTest, RejectsPartialBlocksAndOutOfRangeIo)
{
    block::MemoryBlockDevice device = make_device();
    std::vector<cpu::Byte> partial(TEST_BLOCK_SIZE_BYTES - std::size_t{1});
    std::vector<cpu::Byte> full_block(TEST_BLOCK_SIZE_BYTES);
    std::vector<cpu::Byte> two_blocks(TEST_MULTI_BLOCK_BYTE_COUNT);
    std::vector<cpu::Byte> empty = make_empty_io_buffer();

    EXPECT_THROW(device.read_block(TEST_BLOCK_0, partial), std::invalid_argument);
    EXPECT_THROW(device.write_block(TEST_BLOCK_0, partial), std::invalid_argument);
    EXPECT_THROW(device.read_blocks(TEST_BLOCK_0, partial), std::invalid_argument);
    EXPECT_THROW(device.read_block(TEST_BLOCK_END, full_block), std::out_of_range);
    EXPECT_THROW(device.write_blocks(TEST_BLOCK_3, two_blocks), std::out_of_range);
    EXPECT_THROW(device.read_blocks(TEST_BLOCK_AFTER_END, empty), std::out_of_range);
}

TEST(Stage15BufferCacheTest, ReadsMissThenHitAndTracksStats)
{
    block::MemoryBlockDevice device = make_device();
    const std::vector<cpu::Byte> block_data = make_block(TEST_DATA_SEED_A);
    device.write_block(TEST_BLOCK_0, block_data);

    block::BufferCache cache{device, TEST_CACHE_CAPACITY_BLOCKS};
    std::vector<cpu::Byte> destination(TEST_BLOCK_SIZE_BYTES);
    EXPECT_THAT(cache.geometry().block_count(), Eq(TEST_BLOCK_COUNT));
    EXPECT_THAT(cache.capacity_blocks(), Eq(TEST_CACHE_CAPACITY_BLOCKS));
    EXPECT_THAT(cache.block_size_bytes(), Eq(TEST_BLOCK_SIZE_BYTES));

    cache.read_block(TEST_BLOCK_0, destination);

    EXPECT_THAT(destination, Eq(block_data));
    EXPECT_TRUE(cache.contains(TEST_BLOCK_0));
    EXPECT_FALSE(cache.dirty(TEST_BLOCK_0));
    EXPECT_THAT(cache.valid_count(), Eq(std::size_t{1}));
    EXPECT_THAT(cache.dirty_count(), Eq(std::size_t{0}));
    EXPECT_THAT(cache.stats().miss_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(cache.stats().device_read_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(cache.stats().hit_count(), Eq(std::uint64_t{0}));

    cache.read_block(TEST_BLOCK_0, destination);

    EXPECT_THAT(cache.stats().hit_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(cache.stats().miss_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(cache.stats().device_read_count(), Eq(std::uint64_t{1}));
}

TEST(Stage15BufferCacheTest, WriteIsDirtyUntilFlush)
{
    block::MemoryBlockDevice device = make_device();
    const std::vector<cpu::Byte> old_data(TEST_BLOCK_SIZE_BYTES, TEST_OLD_FILL_BYTE);
    const std::vector<cpu::Byte> new_data = make_block(TEST_DATA_SEED_B);
    device.write_block(TEST_BLOCK_1, old_data);

    block::BufferCache cache{device, TEST_CACHE_CAPACITY_BLOCKS};
    cache.write_block(TEST_BLOCK_1, new_data);

    EXPECT_TRUE(cache.contains(TEST_BLOCK_1));
    EXPECT_TRUE(cache.dirty(TEST_BLOCK_1));
    EXPECT_THAT(cache.dirty_count(), Eq(std::size_t{1}));
    EXPECT_THAT(cache.stats().write_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(cache.stats().miss_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(cache.stats().device_read_count(), Eq(std::uint64_t{0}));
    expect_device_block(device, TEST_BLOCK_1, old_data);

    std::vector<cpu::Byte> cached(TEST_BLOCK_SIZE_BYTES);
    cache.read_block(TEST_BLOCK_1, cached);
    EXPECT_THAT(cached, Eq(new_data));
    EXPECT_THAT(cache.stats().hit_count(), Eq(std::uint64_t{1}));

    cache.flush_block(TEST_BLOCK_1);
    EXPECT_FALSE(cache.dirty(TEST_BLOCK_1));
    EXPECT_THAT(cache.dirty_count(), Eq(std::size_t{0}));
    EXPECT_THAT(cache.stats().device_writeback_count(), Eq(std::uint64_t{1}));
    expect_device_block(device, TEST_BLOCK_1, new_data);

    cache.flush_block(TEST_BLOCK_1);
    EXPECT_THAT(cache.stats().device_writeback_count(), Eq(std::uint64_t{1}));
}

TEST(Stage15BufferCacheTest, DirtyEvictionUsesLeastRecentlyUsedEntry)
{
    block::MemoryBlockDevice device = make_device();
    const std::vector<cpu::Byte> data_a = make_block(TEST_DATA_SEED_A);
    const std::vector<cpu::Byte> data_b = make_block(TEST_DATA_SEED_B);
    const std::vector<cpu::Byte> data_c = make_block(TEST_DATA_SEED_C);
    block::BufferCache cache{device, TEST_CACHE_CAPACITY_BLOCKS};

    cache.write_block(TEST_BLOCK_0, data_a);
    cache.write_block(TEST_BLOCK_1, data_b);

    std::vector<cpu::Byte> touched(TEST_BLOCK_SIZE_BYTES);
    cache.read_block(TEST_BLOCK_1, touched);
    cache.write_block(TEST_BLOCK_2, data_c);

    EXPECT_FALSE(cache.contains(TEST_BLOCK_0));
    EXPECT_TRUE(cache.contains(TEST_BLOCK_1));
    EXPECT_TRUE(cache.contains(TEST_BLOCK_2));
    EXPECT_THAT(cache.valid_count(), Eq(TEST_CACHE_CAPACITY_BLOCKS));
    EXPECT_THAT(cache.stats().dirty_eviction_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(cache.stats().device_writeback_count(), Eq(std::uint64_t{1}));
    expect_device_block(device, TEST_BLOCK_0, data_a);

    cache.flush_all();
    expect_device_block(device, TEST_BLOCK_1, data_b);
    expect_device_block(device, TEST_BLOCK_2, data_c);
}

TEST(Stage15BufferCacheTest, ClearFlushesDirtyBlocksAndInvalidatesEntries)
{
    block::MemoryBlockDevice device = make_device();
    const std::vector<cpu::Byte> data_a = make_block(TEST_DATA_SEED_A);
    const std::vector<cpu::Byte> data_b = make_block(TEST_DATA_SEED_B);
    block::BufferCache cache{device, TEST_CACHE_CAPACITY_BLOCKS};

    cache.write_block(TEST_BLOCK_0, data_a);
    cache.write_block(TEST_BLOCK_1, data_b);
    EXPECT_THAT(cache.dirty_count(), Eq(std::size_t{2}));

    cache.clear();

    EXPECT_FALSE(cache.contains(TEST_BLOCK_0));
    EXPECT_FALSE(cache.contains(TEST_BLOCK_1));
    EXPECT_THAT(cache.valid_count(), Eq(std::size_t{0}));
    EXPECT_THAT(cache.dirty_count(), Eq(std::size_t{0}));
    EXPECT_THAT(cache.stats().device_writeback_count(), Eq(std::uint64_t{2}));
    expect_device_block(device, TEST_BLOCK_0, data_a);
    expect_device_block(device, TEST_BLOCK_1, data_b);

    std::vector<cpu::Byte> destination(TEST_BLOCK_SIZE_BYTES);
    cache.read_block(TEST_BLOCK_0, destination);
    EXPECT_THAT(destination, Eq(data_a));
    EXPECT_THAT(cache.stats().device_read_count(), Eq(std::uint64_t{1}));
}

TEST(Stage15BufferCacheTest, RejectsInvalidCapacitySpansAndAddressesWithoutMutatingCache)
{
    block::MemoryBlockDevice device = make_device();
    block::BufferCache cache{device, TEST_CACHE_CAPACITY_BLOCKS};
    std::vector<cpu::Byte> partial(TEST_BLOCK_SIZE_BYTES - std::size_t{1});
    std::vector<cpu::Byte> full_block(TEST_BLOCK_SIZE_BYTES);

    EXPECT_THROW(static_cast<void>(block::BufferCache{device, std::size_t{0}}), std::invalid_argument);
    EXPECT_THROW(cache.read_block(TEST_BLOCK_0, partial), std::invalid_argument);
    EXPECT_THROW(cache.write_block(TEST_BLOCK_0, partial), std::invalid_argument);
    EXPECT_THROW(cache.read_block(TEST_BLOCK_END, full_block), std::out_of_range);
    EXPECT_THROW(cache.write_block(TEST_BLOCK_END, full_block), std::out_of_range);
    EXPECT_THROW(cache.flush_block(TEST_BLOCK_END), std::out_of_range);

    cache.flush_block(TEST_BLOCK_0);
    EXPECT_THAT(cache.valid_count(), Eq(std::size_t{0}));
    EXPECT_THAT(cache.stats().hit_count(), Eq(std::uint64_t{0}));
    EXPECT_THAT(cache.stats().miss_count(), Eq(std::uint64_t{0}));
    EXPECT_THAT(cache.stats().write_count(), Eq(std::uint64_t{0}));
}
