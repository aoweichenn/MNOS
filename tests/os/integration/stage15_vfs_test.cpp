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
#include <mnos/os/fs/vfs.hpp>

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
constexpr std::uint32_t TEST_INODE_COUNT = std::uint32_t{24};
constexpr cpu::Byte TEST_TEXT_SEED = cpu::Byte{0x21};
constexpr std::size_t TEST_TEXT_SIZE = 96;
constexpr std::size_t TEST_PREFIX_READ_SIZE = 12;

struct MountedVfs
{
    block::MemoryBlockDevice device;
    block::BufferCache cache;
    fs::SimpleFileSystem file_system;
    fs::Vfs vfs;

    MountedVfs() :
        device(block::BlockDeviceGeometry{TEST_BLOCK_SIZE_BYTES, TEST_BLOCK_COUNT}),
        cache(device, TEST_CACHE_BLOCKS),
        file_system(format_and_mount(cache)),
        vfs(file_system)
    {
    }

private:
    [[nodiscard]] static fs::SimpleFileSystem format_and_mount(block::BufferCache& cache)
    {
        fs::SimpleFileSystem::format(cache, fs::SimpleFsFormatOptions{TEST_INODE_COUNT});
        return fs::SimpleFileSystem{cache};
    }
};

[[nodiscard]] std::vector<cpu::Byte> make_text()
{
    std::vector<cpu::Byte> bytes(TEST_TEXT_SIZE);
    for (std::size_t byte_index = std::size_t{0}; byte_index < bytes.size(); ++byte_index)
    {
        bytes[byte_index] = static_cast<cpu::Byte>(TEST_TEXT_SEED + static_cast<cpu::Byte>(byte_index));
    }
    return bytes;
}
}

TEST(Stage15VfsIntegrationTest, CreatesNestedFilesAndFileCursorPersistsThroughRemount)
{
    MountedVfs mounted;

    EXPECT_TRUE(mounted.vfs.root().is_directory());
    const fs::VfsNode home = mounted.vfs.create_directory("/home");
    const fs::VfsNode user = mounted.vfs.create_directory("/home/user");
    const fs::VfsNode readme = mounted.vfs.create_file("/home/user/readme");
    EXPECT_TRUE(home.is_directory());
    EXPECT_TRUE(user.is_directory());
    EXPECT_TRUE(readme.is_file());

    const std::vector<cpu::Byte> text = make_text();
    fs::VfsFile file = mounted.vfs.open_file("/home/user/readme", fs::VfsOpenMode::READ_WRITE);
    EXPECT_TRUE(file.readable());
    EXPECT_TRUE(file.writable());
    EXPECT_THAT(file.write(text), Eq(text.size()));
    EXPECT_THAT(file.offset(), Eq(static_cast<std::uint64_t>(text.size())));
    EXPECT_THAT(file.size_bytes(), Eq(static_cast<std::uint64_t>(text.size())));

    file.seek(std::uint64_t{0});
    std::vector<cpu::Byte> prefix(TEST_PREFIX_READ_SIZE);
    EXPECT_THAT(file.read(prefix), Eq(prefix.size()));
    EXPECT_THAT(prefix.front(), Eq(text.front()));
    EXPECT_THAT(file.offset(), Eq(static_cast<std::uint64_t>(TEST_PREFIX_READ_SIZE)));

    const std::vector<fs::SimpleFsDirectoryEntry> home_entries = mounted.vfs.read_directory("/home");
    EXPECT_THAT(home_entries, SizeIs(1));
    EXPECT_THAT(home_entries.front().name(), Eq("user"));
    EXPECT_TRUE(mounted.vfs.lookup("/home/user/readme").has_value());
    EXPECT_FALSE(mounted.vfs.lookup("/home/user/missing").has_value());

    mounted.file_system.flush();
    block::BufferCache second_cache{mounted.device, TEST_CACHE_BLOCKS};
    fs::SimpleFileSystem second_fs{second_cache};
    fs::Vfs second_vfs{second_fs};
    fs::VfsFile persisted = second_vfs.open_file("/home/user/readme", fs::VfsOpenMode::READ_ONLY);
    std::vector<cpu::Byte> read_back(text.size());
    EXPECT_THAT(persisted.read(read_back), Eq(read_back.size()));
    EXPECT_THAT(read_back, Eq(text));
}

TEST(Stage15VfsIntegrationTest, RejectsInvalidPathsModesAndDirectoryMisuse)
{
    MountedVfs mounted;
    static_cast<void>(mounted.vfs.create_directory("/tmp"));
    static_cast<void>(mounted.vfs.create_file("/tmp/file"));

    EXPECT_THROW(static_cast<void>(mounted.vfs.lookup("tmp/file")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.vfs.create_file("/")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.vfs.create_file("/tmp/.")), std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(mounted.vfs.open_file("/tmp/missing", fs::VfsOpenMode::READ_ONLY)),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(mounted.vfs.open_file("/tmp", fs::VfsOpenMode::READ_ONLY)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(mounted.vfs.open_file("/tmp/file", fs::VfsOpenMode::COUNT)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.vfs.read_directory("/tmp/file")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.vfs.create_file("/tmp/file/child")), std::invalid_argument);

    fs::VfsFile write_only = mounted.vfs.open_file("/tmp/file", fs::VfsOpenMode::WRITE_ONLY);
    std::vector<cpu::Byte> data = make_text();
    EXPECT_THAT(write_only.write(data), Eq(data.size()));
    EXPECT_THROW(static_cast<void>(write_only.read(data)), std::invalid_argument);
    EXPECT_THROW(write_only.seek(static_cast<std::uint64_t>(data.size() + std::size_t{1})), std::invalid_argument);

    fs::VfsFile read_only = mounted.vfs.open_file("/tmp/file", fs::VfsOpenMode::READ_ONLY);
    EXPECT_THROW(static_cast<void>(read_only.write(data)), std::invalid_argument);

    fs::VfsFile closed;
    EXPECT_FALSE(closed.is_open());
    EXPECT_THROW(static_cast<void>(closed.size_bytes()), std::logic_error);
    EXPECT_THROW(static_cast<void>(closed.read(data)), std::logic_error);
}

TEST(Stage15VfsIntegrationTest, ResolvesRootAccessorsAndClosedFileMutationErrors)
{
    MountedVfs mounted;

    const fs::VfsNode root = mounted.vfs.root();
    const std::optional<fs::VfsNode> root_lookup = mounted.vfs.lookup("/");
    ASSERT_TRUE(root_lookup.has_value());
    EXPECT_THAT(root_lookup->inode(), Eq(root.inode()));
    EXPECT_THAT(root_lookup->kind(), Eq(fs::SimpleFsNodeKind::DIRECTORY));
    EXPECT_THAT(root_lookup->size_bytes(), Eq(root.size_bytes()));
    EXPECT_THAT(mounted.vfs.read_directory("/"), SizeIs(0));
    EXPECT_THROW(static_cast<void>(mounted.vfs.read_directory("/missing")), std::out_of_range);

    static_cast<void>(mounted.vfs.create_directory("/bin"));
    const fs::VfsNode tool = mounted.vfs.create_file("/bin/tool");
    EXPECT_THAT(tool.kind(), Eq(fs::SimpleFsNodeKind::FILE));
    EXPECT_THAT(tool.size_bytes(), Eq(std::uint64_t{0}));

    fs::VfsFile file = mounted.vfs.open_file("/bin/tool", fs::VfsOpenMode::READ_WRITE);
    EXPECT_THAT(file.inode(), Eq(tool.inode()));
    EXPECT_THAT(file.mode(), Eq(fs::VfsOpenMode::READ_WRITE));
    EXPECT_FALSE(mounted.vfs.lookup("/bin/tool/child").has_value());
    EXPECT_THROW(static_cast<void>(mounted.vfs.create_file("/missing/leaf")), std::out_of_range);

    const std::string too_long_name(fs::SIMPLE_FS_MAX_NAME_LENGTH + std::size_t{1}, 'x');
    EXPECT_THROW(static_cast<void>(mounted.vfs.lookup("/" + too_long_name)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mounted.vfs.lookup("/bin/..")), std::invalid_argument);

    std::vector<cpu::Byte> data = make_text();
    fs::VfsFile closed;
    EXPECT_THROW(closed.seek(std::uint64_t{0}), std::logic_error);
    EXPECT_THROW(static_cast<void>(closed.write(data)), std::logic_error);
}
