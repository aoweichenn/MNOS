#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/block/block_device.hpp>
#include <mnos/os/block/buffer_cache.hpp>
#include <mnos/os/fs/simple_fs.hpp>

namespace block = mnos::os::block;
namespace cpu = mnos::cpu;
namespace fs = mnos::os::fs;

namespace
{
using ::testing::Eq;
using ::testing::SizeIs;

constexpr std::size_t TEST_BLOCK_SIZE_BYTES = 512;
constexpr std::uint64_t TEST_BLOCK_COUNT = std::uint64_t{128};
constexpr std::size_t TEST_CACHE_BLOCKS = 16;
constexpr std::uint32_t TEST_INODE_COUNT = std::uint32_t{16};
constexpr std::uint32_t TEST_TINY_INODE_COUNT = std::uint32_t{2};
constexpr std::size_t TEST_SMALL_BLOCK_SIZE_BYTES = 64;
constexpr std::uint64_t TEST_SMALL_BLOCK_COUNT = std::uint64_t{8};
constexpr std::uint64_t TEST_ONE_DATA_BLOCK_DEVICE_BLOCK_COUNT = std::uint64_t{4};
constexpr cpu::Byte TEST_DATA_SEED = cpu::Byte{0x35};
constexpr cpu::Byte TEST_OVERWRITE_SEED = cpu::Byte{0x80};
constexpr std::size_t TEST_CROSS_BLOCK_BYTE_COUNT = TEST_BLOCK_SIZE_BYTES + std::size_t{87};
constexpr std::size_t TEST_PARTIAL_READ_OFFSET = TEST_BLOCK_SIZE_BYTES - std::size_t{9};
constexpr std::size_t TEST_PARTIAL_READ_SIZE = 32;
constexpr std::size_t TEST_OVERWRITE_OFFSET = 11;
constexpr std::size_t TEST_OVERWRITE_SIZE = 17;

struct MountedFs
{
    block::MemoryBlockDevice device;
    block::BufferCache cache;
    fs::SimpleFileSystem file_system;

    MountedFs() :
        device(block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, TEST_BLOCK_COUNT}),
        cache(device, TEST_CACHE_BLOCKS),
        file_system(format_and_mount(cache))
    {
    }

private:
    [[nodiscard]] static fs::SimpleFileSystem format_and_mount(block::BufferCache& cache)
    {
        fs::SimpleFileSystem::format(cache, fs::SimpleFsFormatOptions{TEST_INODE_COUNT});
        return fs::SimpleFileSystem{cache};
    }
};

[[nodiscard]] std::vector<cpu::Byte> make_bytes(const std::size_t byte_count, const cpu::Byte seed)
{
    std::vector<cpu::Byte> bytes(byte_count);
    for (std::size_t byte_index = std::size_t{0}; byte_index < bytes.size(); ++byte_index)
    {
        bytes[byte_index] = static_cast<cpu::Byte>(seed + static_cast<cpu::Byte>(byte_index));
    }
    return bytes;
}

}

TEST(Stage15SimpleFsTest, FormatsMountsRootAndRejectsInvalidDevices)
{
    block::MemoryBlockDevice device{block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, TEST_BLOCK_COUNT}};
    block::BufferCache cache{device, TEST_CACHE_BLOCKS};

    EXPECT_THROW(static_cast<void>(fs::SimpleFileSystem{cache}), std::runtime_error);
    EXPECT_THROW(static_cast<void>(fs::SimpleFsFormatOptions{std::uint32_t{0}}), std::invalid_argument);

    block::MemoryBlockDevice small_device{
        block::BlockDeviceGeometry{TEST_SMALL_BLOCK_SIZE_BYTES, TEST_SMALL_BLOCK_COUNT}};
    block::BufferCache small_cache{small_device, TEST_CACHE_BLOCKS};
    EXPECT_THROW(fs::SimpleFileSystem::format(small_cache), std::invalid_argument);

    fs::SimpleFileSystem::format(cache, fs::SimpleFsFormatOptions{TEST_INODE_COUNT});
    fs::SimpleFileSystem file_system{cache};

    const fs::SimpleFsInodeMetadata root = file_system.metadata(file_system.root_inode());
    EXPECT_THAT(root.number(), Eq(fs::InodeNumber::root()));
    EXPECT_TRUE(root.is_directory());
    EXPECT_THAT(root.size_bytes(), Eq(std::uint64_t{0}));
    EXPECT_TRUE(file_system.read_directory(file_system.root_inode()).empty());

    file_system.flush();
    block::BufferCache second_cache{device, TEST_CACHE_BLOCKS};
    fs::SimpleFileSystem remounted{second_cache};
    EXPECT_TRUE(remounted.metadata(remounted.root_inode()).is_directory());
}

TEST(Stage15SimpleFsTest, CreatesDirectoriesFilesAndLookupEntries)
{
    MountedFs mounted;

    const fs::InodeNumber etc = mounted.file_system.create_directory(mounted.file_system.root_inode(), "etc");
    const fs::InodeNumber config = mounted.file_system.create_file(etc, "config");

    const std::optional<fs::SimpleFsDirectoryEntry> etc_entry =
        mounted.file_system.lookup(mounted.file_system.root_inode(), "etc");
    ASSERT_TRUE(etc_entry.has_value());
    EXPECT_THAT(etc_entry->inode(), Eq(etc));
    EXPECT_TRUE(etc_entry->is_directory());

    const std::optional<fs::SimpleFsDirectoryEntry> config_entry = mounted.file_system.lookup(etc, "config");
    ASSERT_TRUE(config_entry.has_value());
    EXPECT_THAT(config_entry->inode(), Eq(config));
    EXPECT_TRUE(config_entry->is_file());

    const std::vector<fs::SimpleFsDirectoryEntry> root_entries =
        mounted.file_system.read_directory(mounted.file_system.root_inode());
    EXPECT_THAT(root_entries, SizeIs(1));
    EXPECT_THAT(root_entries.front().name(), Eq("etc"));

    EXPECT_THROW(static_cast<void>(mounted.file_system.create_file(etc, "config")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.file_system.create_file(etc, "")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.file_system.create_file(etc, "bad/name")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.file_system.create_file(etc, ".")), std::invalid_argument);
    EXPECT_FALSE(mounted.file_system.lookup(etc, "missing").has_value());
}

TEST(Stage15SimpleFsTest, ReadsWritesAcrossBlocksAndPersistsThroughRemount)
{
    MountedFs mounted;
    const fs::InodeNumber file = mounted.file_system.create_file(mounted.file_system.root_inode(), "blob");
    const std::vector<cpu::Byte> data = make_bytes(TEST_CROSS_BLOCK_BYTE_COUNT, TEST_DATA_SEED);

    EXPECT_THAT(mounted.file_system.write_file(file, std::uint64_t{0}, data), Eq(data.size()));
    EXPECT_THAT(mounted.file_system.metadata(file).size_bytes(), Eq(static_cast<std::uint64_t>(data.size())));

    std::vector<cpu::Byte> read_back(data.size());
    EXPECT_THAT(mounted.file_system.read_file(file, std::uint64_t{0}, read_back), Eq(read_back.size()));
    EXPECT_THAT(read_back, Eq(data));

    std::vector<cpu::Byte> partial(TEST_PARTIAL_READ_SIZE);
    EXPECT_THAT(
        mounted.file_system.read_file(file, static_cast<std::uint64_t>(TEST_PARTIAL_READ_OFFSET), partial),
        Eq(partial.size()));
    EXPECT_THAT(
        partial.front(),
        Eq(data[TEST_PARTIAL_READ_OFFSET]));

    const std::vector<cpu::Byte> overwrite = make_bytes(TEST_OVERWRITE_SIZE, TEST_OVERWRITE_SEED);
    EXPECT_THAT(
        mounted.file_system.write_file(file, static_cast<std::uint64_t>(TEST_OVERWRITE_OFFSET), overwrite),
        Eq(overwrite.size()));
    std::copy(overwrite.begin(), overwrite.end(), read_back.begin() + static_cast<std::ptrdiff_t>(TEST_OVERWRITE_OFFSET));

    mounted.file_system.flush();
    block::BufferCache second_cache{mounted.device, TEST_CACHE_BLOCKS};
    fs::SimpleFileSystem remounted{second_cache};
    std::vector<cpu::Byte> persisted(read_back.size());
    EXPECT_THAT(remounted.read_file(file, std::uint64_t{0}, persisted), Eq(persisted.size()));
    EXPECT_THAT(persisted, Eq(read_back));
}

TEST(Stage15SimpleFsTest, RejectsWrongNodeKindsOffsetsAndOversizedFiles)
{
    MountedFs mounted;
    const fs::InodeNumber directory = mounted.file_system.create_directory(mounted.file_system.root_inode(), "dir");
    const fs::InodeNumber file = mounted.file_system.create_file(mounted.file_system.root_inode(), "file");
    std::vector<cpu::Byte> bytes = make_bytes(TEST_BLOCK_SIZE_BYTES, TEST_DATA_SEED);

    EXPECT_THROW(
        static_cast<void>(mounted.file_system.read_file(directory, std::uint64_t{0}, bytes)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(mounted.file_system.write_file(directory, std::uint64_t{0}, bytes)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.file_system.read_directory(file)), std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(mounted.file_system.write_file(file, std::uint64_t{1}, bytes)),
        std::invalid_argument);

    const std::vector<cpu::Byte> too_large(
        fs::SIMPLE_FS_DIRECT_BLOCK_COUNT * TEST_BLOCK_SIZE_BYTES + std::size_t{1},
        TEST_DATA_SEED);
    EXPECT_THROW(
        static_cast<void>(mounted.file_system.write_file(file, std::uint64_t{0}, too_large)),
        std::length_error);
    EXPECT_THROW(
        static_cast<void>(mounted.file_system.metadata(fs::InodeNumber{std::uint32_t{99}})),
        std::out_of_range);
}

TEST(Stage15SimpleFsTest, ReportsNoFreeInodesAndNoFreeDataBlocks)
{
    block::MemoryBlockDevice inode_device{block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, TEST_BLOCK_COUNT}};
    block::BufferCache inode_cache{inode_device, TEST_CACHE_BLOCKS};
    fs::SimpleFileSystem::format(inode_cache, fs::SimpleFsFormatOptions{TEST_TINY_INODE_COUNT});
    fs::SimpleFileSystem inode_fs{inode_cache};
    static_cast<void>(inode_fs.create_file(inode_fs.root_inode(), "one"));
    EXPECT_THROW(static_cast<void>(inode_fs.create_file(inode_fs.root_inode(), "two")), std::length_error);

    block::MemoryBlockDevice block_device{
        block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, TEST_ONE_DATA_BLOCK_DEVICE_BLOCK_COUNT}};
    block::BufferCache block_cache{block_device, TEST_CACHE_BLOCKS};
    fs::SimpleFileSystem::format(block_cache, fs::SimpleFsFormatOptions{TEST_TINY_INODE_COUNT});
    fs::SimpleFileSystem block_fs{block_cache};
    const fs::InodeNumber file = block_fs.create_file(block_fs.root_inode(), "one");
    const std::vector<cpu::Byte> bytes = make_bytes(TEST_BLOCK_SIZE_BYTES, TEST_DATA_SEED);
    EXPECT_THROW(static_cast<void>(block_fs.write_file(file, std::uint64_t{0}, bytes)), std::length_error);
}
