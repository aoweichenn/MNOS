#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

#include <mnos/os/fs/simple_fs.hpp>

namespace
{
constexpr std::uint32_t SIMPLE_FS_MAGIC = std::uint32_t{0x53464E4D};
constexpr std::uint16_t SIMPLE_FS_VERSION = std::uint16_t{1};
constexpr std::size_t SIMPLE_FS_MIN_BLOCK_SIZE_BYTES = 128;
constexpr std::size_t SIMPLE_FS_INODE_RECORD_SIZE_BYTES = 64;
constexpr std::size_t SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES = 64;
constexpr std::size_t SIMPLE_FS_DIRECTORY_ENTRY_NAME_OFFSET = 8;
constexpr std::uint8_t SIMPLE_FS_DISK_KIND_FILE = std::uint8_t{1};
constexpr std::uint8_t SIMPLE_FS_DISK_KIND_DIRECTORY = std::uint8_t{2};
constexpr std::uint8_t SIMPLE_FS_DISK_ALLOCATED = std::uint8_t{1};
constexpr std::uint8_t SIMPLE_FS_DISK_FREE = std::uint8_t{0};
constexpr std::uint16_t SIMPLE_FS_DEFAULT_LINK_COUNT = std::uint16_t{1};
constexpr std::uint64_t SIMPLE_FS_EMPTY_BLOCK = std::uint64_t{0};
constexpr std::size_t SIMPLE_FS_BITS_PER_BYTE = 8;
constexpr std::uint8_t SIMPLE_FS_LOW_BYTE_MASK = std::uint8_t{0xFF};
constexpr char SIMPLE_FS_PATH_SEPARATOR = '/';

constexpr std::size_t SUPERBLOCK_MAGIC_OFFSET = 0;
constexpr std::size_t SUPERBLOCK_VERSION_OFFSET = 4;
constexpr std::size_t SUPERBLOCK_BLOCK_SIZE_OFFSET = 8;
constexpr std::size_t SUPERBLOCK_BLOCK_COUNT_OFFSET = 16;
constexpr std::size_t SUPERBLOCK_INODE_TABLE_START_OFFSET = 24;
constexpr std::size_t SUPERBLOCK_INODE_TABLE_BLOCK_COUNT_OFFSET = 32;
constexpr std::size_t SUPERBLOCK_INODE_COUNT_OFFSET = 36;
constexpr std::size_t SUPERBLOCK_DATA_BITMAP_START_OFFSET = 40;
constexpr std::size_t SUPERBLOCK_DATA_BITMAP_BLOCK_COUNT_OFFSET = 48;
constexpr std::size_t SUPERBLOCK_DATA_START_OFFSET = 56;
constexpr std::size_t SUPERBLOCK_DATA_BLOCK_COUNT_OFFSET = 64;
constexpr std::size_t SUPERBLOCK_ROOT_INODE_OFFSET = 72;

constexpr std::size_t INODE_ALLOCATED_OFFSET = 0;
constexpr std::size_t INODE_KIND_OFFSET = 1;
constexpr std::size_t INODE_LINK_COUNT_OFFSET = 2;
constexpr std::size_t INODE_SIZE_OFFSET = 8;
constexpr std::size_t INODE_DIRECT_BLOCKS_OFFSET = 16;
constexpr std::size_t INODE_DIRECT_BLOCK_STRIDE_BYTES = 8;

constexpr std::size_t DIRENT_ACTIVE_OFFSET = 0;
constexpr std::size_t DIRENT_KIND_OFFSET = 1;
constexpr std::size_t DIRENT_NAME_LENGTH_OFFSET = 2;
constexpr std::size_t DIRENT_INODE_OFFSET = 4;

constexpr const char* SIMPLE_FS_INVALID_FORMAT_OPTIONS_MESSAGE = "simple fs requires at least one inode";
constexpr const char* SIMPLE_FS_INVALID_BLOCK_SIZE_MESSAGE = "simple fs block size is too small for metadata records";
constexpr const char* SIMPLE_FS_INVALID_DEVICE_SIZE_MESSAGE = "simple fs device is too small for metadata and data";
constexpr const char* SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE = "simple fs superblock is invalid";
constexpr const char* SIMPLE_FS_UNSUPPORTED_VERSION_MESSAGE = "simple fs version is unsupported";
constexpr const char* SIMPLE_FS_INVALID_INODE_MESSAGE = "simple fs inode is invalid or not allocated";
constexpr const char* SIMPLE_FS_EXPECTED_DIRECTORY_MESSAGE = "simple fs operation requires a directory inode";
constexpr const char* SIMPLE_FS_EXPECTED_FILE_MESSAGE = "simple fs operation requires a file inode";
constexpr const char* SIMPLE_FS_INVALID_NAME_MESSAGE = "simple fs directory entry name is invalid";
constexpr const char* SIMPLE_FS_DUPLICATE_NAME_MESSAGE = "simple fs directory entry already exists";
constexpr const char* SIMPLE_FS_FILE_TOO_LARGE_MESSAGE = "simple fs file exceeds direct block capacity";
constexpr const char* SIMPLE_FS_OFFSET_GAP_MESSAGE = "simple fs write offset cannot create sparse gaps";
constexpr const char* SIMPLE_FS_NO_FREE_INODE_MESSAGE = "simple fs has no free inode";
constexpr const char* SIMPLE_FS_NO_FREE_BLOCK_MESSAGE = "simple fs has no free data block";
constexpr const char* SIMPLE_FS_CORRUPT_DIRECTORY_MESSAGE = "simple fs directory data is corrupt";

struct SimpleFsLayout
{
    std::size_t block_size_bytes = std::size_t{0};
    std::uint64_t block_count = std::uint64_t{0};
    std::uint64_t inode_table_start = std::uint64_t{0};
    std::uint32_t inode_table_block_count = std::uint32_t{0};
    std::uint32_t inode_count = std::uint32_t{0};
    std::uint64_t data_bitmap_start = std::uint64_t{0};
    std::uint32_t data_bitmap_block_count = std::uint32_t{0};
    std::uint64_t data_start = std::uint64_t{0};
    std::uint64_t data_block_count = std::uint64_t{0};
    mnos::os::fs::InodeNumber root_inode = mnos::os::fs::InodeNumber::root();
};

[[nodiscard]] std::uint64_t ceil_div_u64(const std::uint64_t value, const std::uint64_t divisor)
{
    if (divisor == std::uint64_t{0})
    {
        throw std::invalid_argument{SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE};
    }
    if (value == std::uint64_t{0})
    {
        return std::uint64_t{0};
    }
    return ((value - std::uint64_t{1}) / divisor) + std::uint64_t{1};
}

[[nodiscard]] std::vector<mnos::cpu::Byte> make_zero_block(const std::size_t block_size_bytes)
{
    return std::vector<mnos::cpu::Byte>(block_size_bytes, mnos::cpu::Byte{0});
}

void write_u8(std::span<mnos::cpu::Byte> bytes, const std::size_t offset, const std::uint8_t value)
{
    bytes[offset] = static_cast<mnos::cpu::Byte>(value);
}

[[nodiscard]] std::uint8_t read_u8(std::span<const mnos::cpu::Byte> bytes, const std::size_t offset)
{
    return static_cast<std::uint8_t>(bytes[offset]);
}

void write_u16_le(std::span<mnos::cpu::Byte> bytes, const std::size_t offset, const std::uint16_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint16_t); ++byte_index)
    {
        const std::uint16_t shifted =
            static_cast<std::uint16_t>(value >> static_cast<unsigned>(byte_index * SIMPLE_FS_BITS_PER_BYTE));
        bytes[offset + byte_index] = static_cast<mnos::cpu::Byte>(shifted & SIMPLE_FS_LOW_BYTE_MASK);
    }
}

[[nodiscard]] std::uint16_t read_u16_le(std::span<const mnos::cpu::Byte> bytes, const std::size_t offset)
{
    std::uint16_t value = std::uint16_t{0};
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint16_t); ++byte_index)
    {
        value = static_cast<std::uint16_t>(
            value |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset + byte_index]) <<
                static_cast<unsigned>(byte_index * SIMPLE_FS_BITS_PER_BYTE)));
    }
    return value;
}

void write_u32_le(std::span<mnos::cpu::Byte> bytes, const std::size_t offset, const std::uint32_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint32_t); ++byte_index)
    {
        const std::uint32_t shifted =
            static_cast<std::uint32_t>(value >> static_cast<unsigned>(byte_index * SIMPLE_FS_BITS_PER_BYTE));
        bytes[offset + byte_index] = static_cast<mnos::cpu::Byte>(shifted & SIMPLE_FS_LOW_BYTE_MASK);
    }
}

[[nodiscard]] std::uint32_t read_u32_le(std::span<const mnos::cpu::Byte> bytes, const std::size_t offset)
{
    std::uint32_t value = std::uint32_t{0};
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint32_t); ++byte_index)
    {
        value |=
            static_cast<std::uint32_t>(bytes[offset + byte_index]) <<
            static_cast<unsigned>(byte_index * SIMPLE_FS_BITS_PER_BYTE);
    }
    return value;
}

void write_u64_le(std::span<mnos::cpu::Byte> bytes, const std::size_t offset, const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        const std::uint64_t shifted =
            static_cast<std::uint64_t>(value >> static_cast<unsigned>(byte_index * SIMPLE_FS_BITS_PER_BYTE));
        bytes[offset + byte_index] = static_cast<mnos::cpu::Byte>(shifted & SIMPLE_FS_LOW_BYTE_MASK);
    }
}

[[nodiscard]] std::uint64_t read_u64_le(std::span<const mnos::cpu::Byte> bytes, const std::size_t offset)
{
    std::uint64_t value = std::uint64_t{0};
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        value |=
            static_cast<std::uint64_t>(bytes[offset + byte_index]) <<
            static_cast<unsigned>(byte_index * SIMPLE_FS_BITS_PER_BYTE);
    }
    return value;
}

[[nodiscard]] std::uint8_t node_kind_to_disk(const mnos::os::fs::SimpleFsNodeKind kind)
{
    switch (kind)
    {
    case mnos::os::fs::SimpleFsNodeKind::FILE:
        return SIMPLE_FS_DISK_KIND_FILE;
    case mnos::os::fs::SimpleFsNodeKind::DIRECTORY:
        return SIMPLE_FS_DISK_KIND_DIRECTORY;
    case mnos::os::fs::SimpleFsNodeKind::COUNT:
        break;
    }
    throw std::invalid_argument{SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE};
}

[[nodiscard]] mnos::os::fs::SimpleFsNodeKind node_kind_from_disk(const std::uint8_t disk_value)
{
    if (disk_value == SIMPLE_FS_DISK_KIND_FILE)
    {
        return mnos::os::fs::SimpleFsNodeKind::FILE;
    }
    if (disk_value == SIMPLE_FS_DISK_KIND_DIRECTORY)
    {
        return mnos::os::fs::SimpleFsNodeKind::DIRECTORY;
    }
    return mnos::os::fs::SimpleFsNodeKind::COUNT;
}

[[nodiscard]] std::uint32_t checked_inode_table_block_count(
    const std::uint32_t inode_count,
    const std::size_t block_size_bytes)
{
    const std::uint64_t inode_table_bytes =
        static_cast<std::uint64_t>(inode_count) * static_cast<std::uint64_t>(SIMPLE_FS_INODE_RECORD_SIZE_BYTES);
    const std::uint64_t block_count =
        ceil_div_u64(inode_table_bytes, static_cast<std::uint64_t>(block_size_bytes));
    if (block_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        throw std::length_error{SIMPLE_FS_INVALID_DEVICE_SIZE_MESSAGE};
    }
    return static_cast<std::uint32_t>(block_count);
}

[[nodiscard]] std::uint32_t checked_bitmap_block_count(
    const std::uint64_t data_candidate_block_count,
    const std::size_t block_size_bytes)
{
    const std::uint64_t bits_per_block =
        static_cast<std::uint64_t>(block_size_bytes) * static_cast<std::uint64_t>(SIMPLE_FS_BITS_PER_BYTE);
    const std::uint64_t block_count = ceil_div_u64(data_candidate_block_count, bits_per_block);
    if (block_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        throw std::length_error{SIMPLE_FS_INVALID_DEVICE_SIZE_MESSAGE};
    }
    return static_cast<std::uint32_t>(block_count);
}

[[nodiscard]] SimpleFsLayout make_layout(
    const mnos::os::block::BlockDeviceGeometry& geometry,
    const mnos::os::fs::SimpleFsFormatOptions& options)
{
    if (geometry.block_size_bytes() < SIMPLE_FS_MIN_BLOCK_SIZE_BYTES)
    {
        throw std::invalid_argument{SIMPLE_FS_INVALID_BLOCK_SIZE_MESSAGE};
    }
    if (geometry.block_size_bytes() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        throw std::invalid_argument{SIMPLE_FS_INVALID_BLOCK_SIZE_MESSAGE};
    }

    SimpleFsLayout layout;
    layout.block_size_bytes = geometry.block_size_bytes();
    layout.block_count = geometry.block_count();
    layout.inode_table_start = std::uint64_t{1};
    layout.inode_count = options.inode_count();
    layout.inode_table_block_count = checked_inode_table_block_count(layout.inode_count, layout.block_size_bytes);

    const std::uint64_t metadata_without_bitmap =
        layout.inode_table_start + static_cast<std::uint64_t>(layout.inode_table_block_count);
    if (layout.block_count <= metadata_without_bitmap)
    {
        throw std::length_error{SIMPLE_FS_INVALID_DEVICE_SIZE_MESSAGE};
    }

    const std::uint64_t candidate_data_blocks = layout.block_count - metadata_without_bitmap;
    layout.data_bitmap_start = metadata_without_bitmap;
    layout.data_bitmap_block_count = checked_bitmap_block_count(candidate_data_blocks, layout.block_size_bytes);
    layout.data_start = layout.data_bitmap_start + static_cast<std::uint64_t>(layout.data_bitmap_block_count);
    if (layout.data_start >= layout.block_count)
    {
        throw std::length_error{SIMPLE_FS_INVALID_DEVICE_SIZE_MESSAGE};
    }
    layout.data_block_count = layout.block_count - layout.data_start;
    layout.root_inode = mnos::os::fs::InodeNumber::root();
    return layout;
}

void validate_layout(const SimpleFsLayout& layout, const mnos::os::block::BlockDeviceGeometry& geometry)
{
    if (layout.block_size_bytes != geometry.block_size_bytes() || layout.block_count != geometry.block_count() ||
        layout.block_size_bytes < SIMPLE_FS_MIN_BLOCK_SIZE_BYTES ||
        layout.inode_count < mnos::os::fs::SIMPLE_FS_ROOT_INODE_VALUE ||
        !layout.root_inode.is_valid() || layout.root_inode.value() > layout.inode_count ||
        layout.inode_table_start == std::uint64_t{0} || layout.inode_table_block_count == std::uint32_t{0} ||
        layout.data_bitmap_block_count == std::uint32_t{0} || layout.data_block_count == std::uint64_t{0})
    {
        throw std::runtime_error{SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE};
    }

    const std::uint64_t inode_table_end =
        layout.inode_table_start + static_cast<std::uint64_t>(layout.inode_table_block_count);
    const std::uint64_t bitmap_end =
        layout.data_bitmap_start + static_cast<std::uint64_t>(layout.data_bitmap_block_count);
    const std::uint64_t data_end = layout.data_start + layout.data_block_count;
    const std::uint64_t bitmap_capacity =
        static_cast<std::uint64_t>(layout.data_bitmap_block_count) *
        static_cast<std::uint64_t>(layout.block_size_bytes) * static_cast<std::uint64_t>(SIMPLE_FS_BITS_PER_BYTE);
    if (inode_table_end > layout.data_bitmap_start || bitmap_end > layout.data_start || data_end > layout.block_count)
    {
        throw std::runtime_error{SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE};
    }
    if (bitmap_capacity < layout.data_block_count)
    {
        throw std::runtime_error{SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE};
    }
}

void write_superblock(mnos::os::block::BufferCache& cache, const SimpleFsLayout& layout)
{
    std::vector<mnos::cpu::Byte> block = make_zero_block(layout.block_size_bytes);
    write_u32_le(block, SUPERBLOCK_MAGIC_OFFSET, SIMPLE_FS_MAGIC);
    write_u16_le(block, SUPERBLOCK_VERSION_OFFSET, SIMPLE_FS_VERSION);
    write_u32_le(block, SUPERBLOCK_BLOCK_SIZE_OFFSET, static_cast<std::uint32_t>(layout.block_size_bytes));
    write_u64_le(block, SUPERBLOCK_BLOCK_COUNT_OFFSET, layout.block_count);
    write_u64_le(block, SUPERBLOCK_INODE_TABLE_START_OFFSET, layout.inode_table_start);
    write_u32_le(block, SUPERBLOCK_INODE_TABLE_BLOCK_COUNT_OFFSET, layout.inode_table_block_count);
    write_u32_le(block, SUPERBLOCK_INODE_COUNT_OFFSET, layout.inode_count);
    write_u64_le(block, SUPERBLOCK_DATA_BITMAP_START_OFFSET, layout.data_bitmap_start);
    write_u32_le(block, SUPERBLOCK_DATA_BITMAP_BLOCK_COUNT_OFFSET, layout.data_bitmap_block_count);
    write_u64_le(block, SUPERBLOCK_DATA_START_OFFSET, layout.data_start);
    write_u64_le(block, SUPERBLOCK_DATA_BLOCK_COUNT_OFFSET, layout.data_block_count);
    write_u32_le(block, SUPERBLOCK_ROOT_INODE_OFFSET, layout.root_inode.value());
    cache.write_block(mnos::os::block::BlockAddress{std::uint64_t{0}}, block);
}

[[nodiscard]] SimpleFsLayout read_superblock(mnos::os::block::BufferCache& cache)
{
    std::vector<mnos::cpu::Byte> block(cache.block_size_bytes());
    cache.read_block(mnos::os::block::BlockAddress{std::uint64_t{0}}, block);
    if (read_u32_le(block, SUPERBLOCK_MAGIC_OFFSET) != SIMPLE_FS_MAGIC)
    {
        throw std::runtime_error{SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE};
    }
    if (read_u16_le(block, SUPERBLOCK_VERSION_OFFSET) != SIMPLE_FS_VERSION)
    {
        throw std::runtime_error{SIMPLE_FS_UNSUPPORTED_VERSION_MESSAGE};
    }

    SimpleFsLayout layout;
    layout.block_size_bytes = static_cast<std::size_t>(read_u32_le(block, SUPERBLOCK_BLOCK_SIZE_OFFSET));
    layout.block_count = read_u64_le(block, SUPERBLOCK_BLOCK_COUNT_OFFSET);
    layout.inode_table_start = read_u64_le(block, SUPERBLOCK_INODE_TABLE_START_OFFSET);
    layout.inode_table_block_count = read_u32_le(block, SUPERBLOCK_INODE_TABLE_BLOCK_COUNT_OFFSET);
    layout.inode_count = read_u32_le(block, SUPERBLOCK_INODE_COUNT_OFFSET);
    layout.data_bitmap_start = read_u64_le(block, SUPERBLOCK_DATA_BITMAP_START_OFFSET);
    layout.data_bitmap_block_count = read_u32_le(block, SUPERBLOCK_DATA_BITMAP_BLOCK_COUNT_OFFSET);
    layout.data_start = read_u64_le(block, SUPERBLOCK_DATA_START_OFFSET);
    layout.data_block_count = read_u64_le(block, SUPERBLOCK_DATA_BLOCK_COUNT_OFFSET);
    layout.root_inode = mnos::os::fs::InodeNumber{read_u32_le(block, SUPERBLOCK_ROOT_INODE_OFFSET)};
    validate_layout(layout, cache.geometry());
    return layout;
}

[[nodiscard]] std::uint64_t bitmap_bits_per_block(const std::size_t block_size_bytes)
{
    return static_cast<std::uint64_t>(block_size_bytes) * static_cast<std::uint64_t>(SIMPLE_FS_BITS_PER_BYTE);
}

[[nodiscard]] bool test_bitmap_bit(std::span<const mnos::cpu::Byte> block, const std::uint64_t bit_index)
{
    const std::size_t byte_index = static_cast<std::size_t>(bit_index / SIMPLE_FS_BITS_PER_BYTE);
    const std::uint8_t bit_offset = static_cast<std::uint8_t>(bit_index % SIMPLE_FS_BITS_PER_BYTE);
    const std::uint8_t mask = static_cast<std::uint8_t>(std::uint8_t{1} << bit_offset);
    return (block[byte_index] & mask) != mnos::cpu::Byte{0};
}

void set_bitmap_bit(std::span<mnos::cpu::Byte> block, const std::uint64_t bit_index)
{
    const std::size_t byte_index = static_cast<std::size_t>(bit_index / SIMPLE_FS_BITS_PER_BYTE);
    const std::uint8_t bit_offset = static_cast<std::uint8_t>(bit_index % SIMPLE_FS_BITS_PER_BYTE);
    const std::uint8_t mask = static_cast<std::uint8_t>(std::uint8_t{1} << bit_offset);
    block[byte_index] = static_cast<mnos::cpu::Byte>(block[byte_index] | mask);
}

[[nodiscard]] std::vector<mnos::cpu::Byte> serialize_directory_entry(
    const mnos::os::fs::SimpleFsDirectoryEntry& entry)
{
    if (entry.name().empty() || entry.name().size() > mnos::os::fs::SIMPLE_FS_MAX_NAME_LENGTH)
    {
        throw std::invalid_argument{SIMPLE_FS_INVALID_NAME_MESSAGE};
    }

    std::vector<mnos::cpu::Byte> record(SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES, mnos::cpu::Byte{0});
    write_u8(record, DIRENT_ACTIVE_OFFSET, SIMPLE_FS_DISK_ALLOCATED);
    write_u8(record, DIRENT_KIND_OFFSET, node_kind_to_disk(entry.kind()));
    write_u16_le(record, DIRENT_NAME_LENGTH_OFFSET, static_cast<std::uint16_t>(entry.name().size()));
    write_u32_le(record, DIRENT_INODE_OFFSET, entry.inode().value());
    for (std::size_t char_index = std::size_t{0}; char_index < entry.name().size(); ++char_index)
    {
        record[SIMPLE_FS_DIRECTORY_ENTRY_NAME_OFFSET + char_index] =
            static_cast<mnos::cpu::Byte>(entry.name()[char_index]);
    }
    return record;
}

[[nodiscard]] std::optional<mnos::os::fs::SimpleFsDirectoryEntry> parse_directory_entry(
    std::span<const mnos::cpu::Byte> record)
{
    if (read_u8(record, DIRENT_ACTIVE_OFFSET) == SIMPLE_FS_DISK_FREE)
    {
        return std::nullopt;
    }

    const std::uint16_t name_length = read_u16_le(record, DIRENT_NAME_LENGTH_OFFSET);
    if (name_length == std::uint16_t{0} || name_length > mnos::os::fs::SIMPLE_FS_MAX_NAME_LENGTH)
    {
        throw std::runtime_error{SIMPLE_FS_CORRUPT_DIRECTORY_MESSAGE};
    }

    std::string name;
    name.resize(name_length);
    for (std::size_t char_index = std::size_t{0}; char_index < static_cast<std::size_t>(name_length); ++char_index)
    {
        name[char_index] = static_cast<char>(record[SIMPLE_FS_DIRECTORY_ENTRY_NAME_OFFSET + char_index]);
    }

    const mnos::os::fs::SimpleFsNodeKind kind = node_kind_from_disk(read_u8(record, DIRENT_KIND_OFFSET));
    if (kind == mnos::os::fs::SimpleFsNodeKind::COUNT)
    {
        throw std::runtime_error{SIMPLE_FS_CORRUPT_DIRECTORY_MESSAGE};
    }

    return mnos::os::fs::SimpleFsDirectoryEntry{
        std::move(name),
        mnos::os::fs::InodeNumber{read_u32_le(record, DIRENT_INODE_OFFSET)},
        kind};
}
}

namespace mnos::os::fs
{
struct SimpleFileSystem::InodeRecord
{
    InodeNumber number = InodeNumber::invalid();
    SimpleFsNodeKind kind = SimpleFsNodeKind::COUNT;
    std::uint64_t size_bytes = std::uint64_t{0};
    std::array<block::BlockAddress, SIMPLE_FS_DIRECT_BLOCK_COUNT> direct_blocks{};
    bool allocated = false;
};

SimpleFsFormatOptions::SimpleFsFormatOptions(const std::uint32_t inode_count) : inode_count_(inode_count)
{
    if (this->inode_count_ < SIMPLE_FS_ROOT_INODE_VALUE)
    {
        throw std::invalid_argument{SIMPLE_FS_INVALID_FORMAT_OPTIONS_MESSAGE};
    }
}

std::uint32_t SimpleFsFormatOptions::inode_count() const noexcept
{
    return this->inode_count_;
}

SimpleFsInodeMetadata::SimpleFsInodeMetadata(
    InodeNumber number,
    const SimpleFsNodeKind kind,
    const std::uint64_t size_bytes) noexcept :
    number_(number),
    kind_(kind),
    size_bytes_(size_bytes)
{
}

InodeNumber SimpleFsInodeMetadata::number() const noexcept
{
    return this->number_;
}

SimpleFsNodeKind SimpleFsInodeMetadata::kind() const noexcept
{
    return this->kind_;
}

std::uint64_t SimpleFsInodeMetadata::size_bytes() const noexcept
{
    return this->size_bytes_;
}

bool SimpleFsInodeMetadata::is_file() const noexcept
{
    return this->kind_ == SimpleFsNodeKind::FILE;
}

bool SimpleFsInodeMetadata::is_directory() const noexcept
{
    return this->kind_ == SimpleFsNodeKind::DIRECTORY;
}

SimpleFsDirectoryEntry::SimpleFsDirectoryEntry(
    std::string name,
    InodeNumber inode,
    const SimpleFsNodeKind kind) :
    name_(std::move(name)),
    inode_(inode),
    kind_(kind)
{
}

std::string_view SimpleFsDirectoryEntry::name() const noexcept
{
    return this->name_;
}

InodeNumber SimpleFsDirectoryEntry::inode() const noexcept
{
    return this->inode_;
}

SimpleFsNodeKind SimpleFsDirectoryEntry::kind() const noexcept
{
    return this->kind_;
}

bool SimpleFsDirectoryEntry::is_file() const noexcept
{
    return this->kind_ == SimpleFsNodeKind::FILE;
}

bool SimpleFsDirectoryEntry::is_directory() const noexcept
{
    return this->kind_ == SimpleFsNodeKind::DIRECTORY;
}

void SimpleFileSystem::format(block::BufferCache& cache, const SimpleFsFormatOptions options)
{
    const SimpleFsLayout layout = make_layout(cache.geometry(), options);
    const std::vector<cpu::Byte> zero_block = make_zero_block(layout.block_size_bytes);
    for (std::uint64_t block_index = std::uint64_t{0}; block_index < layout.block_count; ++block_index)
    {
        cache.write_block(block::BlockAddress{block_index}, zero_block);
    }

    write_superblock(cache, layout);

    std::vector<cpu::Byte> inode_block = make_zero_block(layout.block_size_bytes);
    write_u8(inode_block, INODE_ALLOCATED_OFFSET, SIMPLE_FS_DISK_ALLOCATED);
    write_u8(inode_block, INODE_KIND_OFFSET, node_kind_to_disk(SimpleFsNodeKind::DIRECTORY));
    write_u16_le(inode_block, INODE_LINK_COUNT_OFFSET, SIMPLE_FS_DEFAULT_LINK_COUNT);
    write_u64_le(inode_block, INODE_SIZE_OFFSET, std::uint64_t{0});
    cache.write_block(block::BlockAddress{layout.inode_table_start}, inode_block);
    cache.flush_all();
}

SimpleFileSystem::SimpleFileSystem(block::BufferCache& cache) : cache_(cache)
{
    const SimpleFsLayout layout = read_superblock(cache);
    this->block_size_bytes_ = layout.block_size_bytes;
    this->block_count_ = layout.block_count;
    this->inode_table_start_ = layout.inode_table_start;
    this->inode_table_block_count_ = layout.inode_table_block_count;
    this->inode_count_ = layout.inode_count;
    this->data_bitmap_start_ = layout.data_bitmap_start;
    this->data_bitmap_block_count_ = layout.data_bitmap_block_count;
    this->data_start_ = layout.data_start;
    this->data_block_count_ = layout.data_block_count;
    this->root_inode_ = layout.root_inode;

    const InodeRecord root = this->read_inode(this->root_inode_);
    if (!root.allocated || root.kind != SimpleFsNodeKind::DIRECTORY)
    {
        throw std::runtime_error{SIMPLE_FS_INVALID_SUPERBLOCK_MESSAGE};
    }
}

SimpleFileSystem::~SimpleFileSystem() = default;

InodeNumber SimpleFileSystem::root_inode() const noexcept
{
    return this->root_inode_;
}

SimpleFsInodeMetadata SimpleFileSystem::metadata(const InodeNumber inode) const
{
    const InodeRecord record = this->read_inode(inode);
    if (!record.allocated)
    {
        throw std::out_of_range{SIMPLE_FS_INVALID_INODE_MESSAGE};
    }
    return SimpleFsInodeMetadata{record.number, record.kind, record.size_bytes};
}

std::optional<SimpleFsDirectoryEntry> SimpleFileSystem::lookup(
    const InodeNumber directory,
    const std::string_view name) const
{
    this->validate_directory_name(name);
    const std::vector<SimpleFsDirectoryEntry> entries = this->read_directory(directory);
    for (const SimpleFsDirectoryEntry& entry : entries)
    {
        if (entry.name() == name)
        {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<SimpleFsDirectoryEntry> SimpleFileSystem::read_directory(const InodeNumber directory) const
{
    const InodeRecord record = this->read_inode(directory);
    this->validate_directory_inode(record);
    if (record.size_bytes % SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES != std::uint64_t{0})
    {
        throw std::runtime_error{SIMPLE_FS_CORRUPT_DIRECTORY_MESSAGE};
    }

    std::vector<cpu::Byte> bytes(static_cast<std::size_t>(record.size_bytes));
    static_cast<void>(this->read_inode_bytes(record, std::uint64_t{0}, bytes));

    std::vector<SimpleFsDirectoryEntry> entries;
    const std::size_t entry_count = bytes.size() / SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES;
    entries.reserve(entry_count);
    for (std::size_t entry_index = std::size_t{0}; entry_index < entry_count; ++entry_index)
    {
        const std::size_t offset = entry_index * SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES;
        const std::span<const cpu::Byte> entry_record{
            bytes.data() + static_cast<std::ptrdiff_t>(offset),
            SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES};
        std::optional<SimpleFsDirectoryEntry> entry = parse_directory_entry(entry_record);
        if (entry.has_value())
        {
            entries.push_back(std::move(entry.value()));
        }
    }
    return entries;
}

InodeNumber SimpleFileSystem::create_file(const InodeNumber parent_directory, const std::string_view name)
{
    this->validate_directory_name(name);
    if (this->lookup(parent_directory, name).has_value())
    {
        throw std::invalid_argument{SIMPLE_FS_DUPLICATE_NAME_MESSAGE};
    }

    InodeRecord parent = this->read_inode(parent_directory);
    this->validate_directory_inode(parent);
    if (this->additional_blocks_needed(parent, parent.size_bytes + SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES) >
        this->free_data_block_count())
    {
        throw std::length_error{SIMPLE_FS_NO_FREE_BLOCK_MESSAGE};
    }

    const InodeNumber inode = this->allocate_inode(SimpleFsNodeKind::FILE);
    this->append_directory_entry(parent, SimpleFsDirectoryEntry{std::string{name}, inode, SimpleFsNodeKind::FILE});
    return inode;
}

InodeNumber SimpleFileSystem::create_directory(const InodeNumber parent_directory, const std::string_view name)
{
    this->validate_directory_name(name);
    if (this->lookup(parent_directory, name).has_value())
    {
        throw std::invalid_argument{SIMPLE_FS_DUPLICATE_NAME_MESSAGE};
    }

    InodeRecord parent = this->read_inode(parent_directory);
    this->validate_directory_inode(parent);
    if (this->additional_blocks_needed(parent, parent.size_bytes + SIMPLE_FS_DIRECTORY_ENTRY_SIZE_BYTES) >
        this->free_data_block_count())
    {
        throw std::length_error{SIMPLE_FS_NO_FREE_BLOCK_MESSAGE};
    }

    const InodeNumber inode = this->allocate_inode(SimpleFsNodeKind::DIRECTORY);
    this->append_directory_entry(parent, SimpleFsDirectoryEntry{std::string{name}, inode, SimpleFsNodeKind::DIRECTORY});
    return inode;
}

std::size_t SimpleFileSystem::read_file(
    const InodeNumber file,
    const std::uint64_t offset,
    const std::span<cpu::Byte> destination) const
{
    const InodeRecord record = this->read_inode(file);
    this->validate_file_inode(record);
    return this->read_inode_bytes(record, offset, destination);
}

std::size_t SimpleFileSystem::write_file(
    const InodeNumber file,
    const std::uint64_t offset,
    const std::span<const cpu::Byte> source)
{
    InodeRecord record = this->read_inode(file);
    this->validate_file_inode(record);
    const std::size_t byte_count = this->write_inode_bytes(record, offset, source);
    this->write_inode(record);
    return byte_count;
}

void SimpleFileSystem::flush()
{
    this->cache_.flush_all();
}

SimpleFileSystem::InodeRecord SimpleFileSystem::read_inode(const InodeNumber inode) const
{
    this->validate_inode_number(inode);
    const std::uint64_t inode_index = static_cast<std::uint64_t>(inode.value() - SIMPLE_FS_ROOT_INODE_VALUE);
    const std::uint64_t byte_offset = inode_index * SIMPLE_FS_INODE_RECORD_SIZE_BYTES;
    const std::uint64_t block_offset = byte_offset / static_cast<std::uint64_t>(this->block_size_bytes_);
    const std::size_t offset_in_block =
        static_cast<std::size_t>(byte_offset % static_cast<std::uint64_t>(this->block_size_bytes_));

    std::vector<cpu::Byte> block(this->block_size_bytes_);
    this->cache_.read_block(block::BlockAddress{this->inode_table_start_ + block_offset}, block);
    const std::span<const cpu::Byte> record{
        block.data() + static_cast<std::ptrdiff_t>(offset_in_block),
        SIMPLE_FS_INODE_RECORD_SIZE_BYTES};

    InodeRecord inode_record;
    inode_record.number = inode;
    inode_record.allocated = read_u8(record, INODE_ALLOCATED_OFFSET) == SIMPLE_FS_DISK_ALLOCATED;
    if (!inode_record.allocated)
    {
        return inode_record;
    }

    inode_record.kind = node_kind_from_disk(read_u8(record, INODE_KIND_OFFSET));
    if (inode_record.kind == SimpleFsNodeKind::COUNT)
    {
        throw std::runtime_error{SIMPLE_FS_INVALID_INODE_MESSAGE};
    }
    inode_record.size_bytes = read_u64_le(record, INODE_SIZE_OFFSET);
    if (inode_record.size_bytes > this->max_file_size_bytes())
    {
        throw std::runtime_error{SIMPLE_FS_INVALID_INODE_MESSAGE};
    }
    for (std::size_t block_index = std::size_t{0}; block_index < inode_record.direct_blocks.size(); ++block_index)
    {
        inode_record.direct_blocks[block_index] =
            block::BlockAddress{read_u64_le(record, INODE_DIRECT_BLOCKS_OFFSET + block_index * INODE_DIRECT_BLOCK_STRIDE_BYTES)};
        if (inode_record.direct_blocks[block_index].value() != SIMPLE_FS_EMPTY_BLOCK &&
            (inode_record.direct_blocks[block_index].value() < this->data_start_ ||
             inode_record.direct_blocks[block_index].value() >= this->data_start_ + this->data_block_count_))
        {
            throw std::runtime_error{SIMPLE_FS_INVALID_INODE_MESSAGE};
        }
    }
    return inode_record;
}

void SimpleFileSystem::write_inode(const InodeRecord& inode)
{
    this->validate_inode_number(inode.number);
    const std::uint64_t inode_index = static_cast<std::uint64_t>(inode.number.value() - SIMPLE_FS_ROOT_INODE_VALUE);
    const std::uint64_t byte_offset = inode_index * SIMPLE_FS_INODE_RECORD_SIZE_BYTES;
    const std::uint64_t block_offset = byte_offset / static_cast<std::uint64_t>(this->block_size_bytes_);
    const std::size_t offset_in_block =
        static_cast<std::size_t>(byte_offset % static_cast<std::uint64_t>(this->block_size_bytes_));

    std::vector<cpu::Byte> block(this->block_size_bytes_);
    this->cache_.read_block(block::BlockAddress{this->inode_table_start_ + block_offset}, block);
    std::span<cpu::Byte> record{
        block.data() + static_cast<std::ptrdiff_t>(offset_in_block),
        SIMPLE_FS_INODE_RECORD_SIZE_BYTES};
    std::fill(record.begin(), record.end(), cpu::Byte{0});
    write_u8(record, INODE_ALLOCATED_OFFSET, inode.allocated ? SIMPLE_FS_DISK_ALLOCATED : SIMPLE_FS_DISK_FREE);
    if (inode.allocated)
    {
        write_u8(record, INODE_KIND_OFFSET, node_kind_to_disk(inode.kind));
        write_u16_le(record, INODE_LINK_COUNT_OFFSET, SIMPLE_FS_DEFAULT_LINK_COUNT);
        write_u64_le(record, INODE_SIZE_OFFSET, inode.size_bytes);
        for (std::size_t block_index = std::size_t{0}; block_index < inode.direct_blocks.size(); ++block_index)
        {
            write_u64_le(
                record,
                INODE_DIRECT_BLOCKS_OFFSET + block_index * INODE_DIRECT_BLOCK_STRIDE_BYTES,
                inode.direct_blocks[block_index].value());
        }
    }
    this->cache_.write_block(block::BlockAddress{this->inode_table_start_ + block_offset}, block);
}

InodeNumber SimpleFileSystem::allocate_inode(const SimpleFsNodeKind kind)
{
    for (std::uint32_t inode_value = SIMPLE_FS_ROOT_INODE_VALUE; inode_value <= this->inode_count_; ++inode_value)
    {
        InodeRecord record = this->read_inode(InodeNumber{inode_value});
        if (!record.allocated)
        {
            record.allocated = true;
            record.kind = kind;
            record.size_bytes = std::uint64_t{0};
            record.direct_blocks.fill(block::BlockAddress{SIMPLE_FS_EMPTY_BLOCK});
            this->write_inode(record);
            return record.number;
        }
    }
    throw std::length_error{SIMPLE_FS_NO_FREE_INODE_MESSAGE};
}

block::BlockAddress SimpleFileSystem::allocate_data_block()
{
    const std::uint64_t bits_per_block = bitmap_bits_per_block(this->block_size_bytes_);
    for (std::uint32_t bitmap_block_index = std::uint32_t{0}; bitmap_block_index < this->data_bitmap_block_count_;
         ++bitmap_block_index)
    {
        std::vector<cpu::Byte> bitmap(this->block_size_bytes_);
        const block::BlockAddress bitmap_block{this->data_bitmap_start_ + static_cast<std::uint64_t>(bitmap_block_index)};
        this->cache_.read_block(bitmap_block, bitmap);

        const std::uint64_t data_base = static_cast<std::uint64_t>(bitmap_block_index) * bits_per_block;
        if (data_base >= this->data_block_count_)
        {
            break;
        }
        const std::uint64_t bits_to_scan = std::min(bits_per_block, this->data_block_count_ - data_base);
        for (std::uint64_t bit_index = std::uint64_t{0}; bit_index < bits_to_scan; ++bit_index)
        {
            if (!test_bitmap_bit(bitmap, bit_index))
            {
                set_bitmap_bit(bitmap, bit_index);
                this->cache_.write_block(bitmap_block, bitmap);

                const block::BlockAddress data_block{this->data_start_ + data_base + bit_index};
                const std::vector<cpu::Byte> zero_block = make_zero_block(this->block_size_bytes_);
                this->cache_.write_block(data_block, zero_block);
                return data_block;
            }
        }
    }
    throw std::length_error{SIMPLE_FS_NO_FREE_BLOCK_MESSAGE};
}

std::uint64_t SimpleFileSystem::free_data_block_count() const
{
    std::uint64_t free_count = std::uint64_t{0};
    const std::uint64_t bits_per_block = bitmap_bits_per_block(this->block_size_bytes_);
    for (std::uint32_t bitmap_block_index = std::uint32_t{0}; bitmap_block_index < this->data_bitmap_block_count_;
         ++bitmap_block_index)
    {
        std::vector<cpu::Byte> bitmap(this->block_size_bytes_);
        this->cache_.read_block(
            block::BlockAddress{this->data_bitmap_start_ + static_cast<std::uint64_t>(bitmap_block_index)},
            bitmap);

        const std::uint64_t data_base = static_cast<std::uint64_t>(bitmap_block_index) * bits_per_block;
        if (data_base >= this->data_block_count_)
        {
            break;
        }
        const std::uint64_t bits_to_scan = std::min(bits_per_block, this->data_block_count_ - data_base);
        for (std::uint64_t bit_index = std::uint64_t{0}; bit_index < bits_to_scan; ++bit_index)
        {
            if (!test_bitmap_bit(bitmap, bit_index))
            {
                ++free_count;
            }
        }
    }
    return free_count;
}

std::uint64_t SimpleFileSystem::additional_blocks_needed(
    const InodeRecord& inode,
    const std::uint64_t end_size) const
{
    if (end_size > this->max_file_size_bytes())
    {
        throw std::length_error{SIMPLE_FS_FILE_TOO_LARGE_MESSAGE};
    }

    const std::uint64_t needed_blocks =
        ceil_div_u64(end_size, static_cast<std::uint64_t>(this->block_size_bytes_));
    std::uint64_t current_blocks = std::uint64_t{0};
    for (const block::BlockAddress data_block : inode.direct_blocks)
    {
        if (data_block.value() != SIMPLE_FS_EMPTY_BLOCK)
        {
            ++current_blocks;
        }
    }
    return needed_blocks > current_blocks ? needed_blocks - current_blocks : std::uint64_t{0};
}

std::size_t SimpleFileSystem::read_inode_bytes(
    const InodeRecord& inode,
    const std::uint64_t offset,
    const std::span<cpu::Byte> destination) const
{
    if (offset >= inode.size_bytes || destination.empty())
    {
        return std::size_t{0};
    }

    std::uint64_t remaining =
        std::min(static_cast<std::uint64_t>(destination.size()), inode.size_bytes - offset);
    std::uint64_t current_offset = offset;
    std::size_t destination_offset = std::size_t{0};
    std::vector<cpu::Byte> block_bytes(this->block_size_bytes_);

    while (remaining > std::uint64_t{0})
    {
        const std::uint64_t logical_block_index =
            current_offset / static_cast<std::uint64_t>(this->block_size_bytes_);
        const std::size_t offset_in_block =
            static_cast<std::size_t>(current_offset % static_cast<std::uint64_t>(this->block_size_bytes_));
        const std::size_t chunk_size = static_cast<std::size_t>(
            std::min(remaining, static_cast<std::uint64_t>(this->block_size_bytes_ - offset_in_block)));

        const block::BlockAddress data_block = inode.direct_blocks[static_cast<std::size_t>(logical_block_index)];
        if (data_block.value() == SIMPLE_FS_EMPTY_BLOCK)
        {
            std::fill_n(destination.begin() + static_cast<std::ptrdiff_t>(destination_offset), chunk_size, cpu::Byte{0});
        }
        else
        {
            this->cache_.read_block(data_block, block_bytes);
            std::copy_n(
                block_bytes.begin() + static_cast<std::ptrdiff_t>(offset_in_block),
                chunk_size,
                destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
        }

        current_offset += static_cast<std::uint64_t>(chunk_size);
        destination_offset += chunk_size;
        remaining -= static_cast<std::uint64_t>(chunk_size);
    }

    return destination_offset;
}

std::size_t SimpleFileSystem::write_inode_bytes(
    InodeRecord& inode,
    const std::uint64_t offset,
    const std::span<const cpu::Byte> source)
{
    if (offset > inode.size_bytes)
    {
        throw std::invalid_argument{SIMPLE_FS_OFFSET_GAP_MESSAGE};
    }
    if (source.empty())
    {
        return std::size_t{0};
    }
    if (static_cast<std::uint64_t>(source.size()) > std::numeric_limits<std::uint64_t>::max() - offset)
    {
        throw std::length_error{SIMPLE_FS_FILE_TOO_LARGE_MESSAGE};
    }

    const std::uint64_t end_offset = offset + static_cast<std::uint64_t>(source.size());
    const std::uint64_t new_blocks = this->additional_blocks_needed(inode, end_offset);
    if (new_blocks > this->free_data_block_count())
    {
        throw std::length_error{SIMPLE_FS_NO_FREE_BLOCK_MESSAGE};
    }

    std::uint64_t remaining = static_cast<std::uint64_t>(source.size());
    std::uint64_t current_offset = offset;
    std::size_t source_offset = std::size_t{0};
    std::vector<cpu::Byte> block_bytes(this->block_size_bytes_);

    while (remaining > std::uint64_t{0})
    {
        const std::uint64_t logical_block_index =
            current_offset / static_cast<std::uint64_t>(this->block_size_bytes_);
        const std::size_t offset_in_block =
            static_cast<std::size_t>(current_offset % static_cast<std::uint64_t>(this->block_size_bytes_));
        const std::size_t chunk_size = static_cast<std::size_t>(
            std::min(remaining, static_cast<std::uint64_t>(this->block_size_bytes_ - offset_in_block)));
        const std::size_t direct_block_index = static_cast<std::size_t>(logical_block_index);
        block::BlockAddress data_block = inode.direct_blocks[direct_block_index];

        if (data_block.value() == SIMPLE_FS_EMPTY_BLOCK)
        {
            data_block = this->allocate_data_block();
            inode.direct_blocks[direct_block_index] = data_block;
            std::fill(block_bytes.begin(), block_bytes.end(), cpu::Byte{0});
        }
        else
        {
            this->cache_.read_block(data_block, block_bytes);
        }

        std::copy_n(
            source.begin() + static_cast<std::ptrdiff_t>(source_offset),
            chunk_size,
            block_bytes.begin() + static_cast<std::ptrdiff_t>(offset_in_block));
        this->cache_.write_block(data_block, block_bytes);

        current_offset += static_cast<std::uint64_t>(chunk_size);
        source_offset += chunk_size;
        remaining -= static_cast<std::uint64_t>(chunk_size);
    }

    inode.size_bytes = std::max(inode.size_bytes, end_offset);
    return source.size();
}

void SimpleFileSystem::append_directory_entry(InodeRecord& directory, const SimpleFsDirectoryEntry& entry)
{
    this->validate_directory_inode(directory);
    const std::vector<cpu::Byte> record = serialize_directory_entry(entry);
    static_cast<void>(this->write_inode_bytes(directory, directory.size_bytes, record));
    this->write_inode(directory);
}

void SimpleFileSystem::validate_inode_number(const InodeNumber inode) const
{
    if (!inode.is_valid() || inode.value() > this->inode_count_)
    {
        throw std::out_of_range{SIMPLE_FS_INVALID_INODE_MESSAGE};
    }
}

void SimpleFileSystem::validate_directory_name(const std::string_view name) const
{
    if (name.empty() || name.size() > SIMPLE_FS_MAX_NAME_LENGTH ||
        name.find(SIMPLE_FS_PATH_SEPARATOR) != std::string_view::npos || name == "." || name == "..")
    {
        throw std::invalid_argument{SIMPLE_FS_INVALID_NAME_MESSAGE};
    }
}

void SimpleFileSystem::validate_directory_inode(const InodeRecord& inode) const
{
    if (!inode.allocated)
    {
        throw std::out_of_range{SIMPLE_FS_INVALID_INODE_MESSAGE};
    }
    if (inode.kind != SimpleFsNodeKind::DIRECTORY)
    {
        throw std::invalid_argument{SIMPLE_FS_EXPECTED_DIRECTORY_MESSAGE};
    }
}

void SimpleFileSystem::validate_file_inode(const InodeRecord& inode) const
{
    if (!inode.allocated)
    {
        throw std::out_of_range{SIMPLE_FS_INVALID_INODE_MESSAGE};
    }
    if (inode.kind != SimpleFsNodeKind::FILE)
    {
        throw std::invalid_argument{SIMPLE_FS_EXPECTED_FILE_MESSAGE};
    }
}

std::uint64_t SimpleFileSystem::max_file_size_bytes() const noexcept
{
    return static_cast<std::uint64_t>(SIMPLE_FS_DIRECT_BLOCK_COUNT) *
           static_cast<std::uint64_t>(this->block_size_bytes_);
}
}
