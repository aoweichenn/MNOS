#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/os/block/buffer_cache.hpp>

namespace mnos::os::fs
{
inline constexpr std::uint32_t SIMPLE_FS_DEFAULT_INODE_COUNT = std::uint32_t{64};
inline constexpr std::uint32_t SIMPLE_FS_ROOT_INODE_VALUE = std::uint32_t{1};
inline constexpr std::uint32_t SIMPLE_FS_INVALID_INODE_VALUE = std::uint32_t{0};
inline constexpr std::size_t SIMPLE_FS_MAX_NAME_LENGTH = 56;
inline constexpr std::size_t SIMPLE_FS_DIRECT_BLOCK_COUNT = 6;

enum class SimpleFsNodeKind : std::uint8_t
{
    FILE,
    DIRECTORY,
    COUNT
};

class InodeNumber final
{
public:
    using value_type = std::uint32_t;

    constexpr InodeNumber() noexcept = default;
    explicit constexpr InodeNumber(value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] static constexpr InodeNumber invalid() noexcept
    {
        return InodeNumber{SIMPLE_FS_INVALID_INODE_VALUE};
    }

    [[nodiscard]] static constexpr InodeNumber root() noexcept
    {
        return InodeNumber{SIMPLE_FS_ROOT_INODE_VALUE};
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return this->value_ != SIMPLE_FS_INVALID_INODE_VALUE;
    }

    [[nodiscard]] friend constexpr auto operator<=>(const InodeNumber&, const InodeNumber&) noexcept = default;

private:
    value_type value_ = SIMPLE_FS_INVALID_INODE_VALUE;
};

class SimpleFsFormatOptions final
{
public:
    SimpleFsFormatOptions() noexcept = default;
    explicit SimpleFsFormatOptions(std::uint32_t inode_count);

    [[nodiscard]] std::uint32_t inode_count() const noexcept;

private:
    std::uint32_t inode_count_ = SIMPLE_FS_DEFAULT_INODE_COUNT;
};

class SimpleFsInodeMetadata final
{
public:
    SimpleFsInodeMetadata() noexcept = default;
    SimpleFsInodeMetadata(InodeNumber number, SimpleFsNodeKind kind, std::uint64_t size_bytes) noexcept;

    [[nodiscard]] InodeNumber number() const noexcept;
    [[nodiscard]] SimpleFsNodeKind kind() const noexcept;
    [[nodiscard]] std::uint64_t size_bytes() const noexcept;
    [[nodiscard]] bool is_file() const noexcept;
    [[nodiscard]] bool is_directory() const noexcept;

private:
    InodeNumber number_ = InodeNumber::invalid();
    SimpleFsNodeKind kind_ = SimpleFsNodeKind::COUNT;
    std::uint64_t size_bytes_ = std::uint64_t{0};
};

class SimpleFsDirectoryEntry final
{
public:
    SimpleFsDirectoryEntry() = default;
    SimpleFsDirectoryEntry(std::string name, InodeNumber inode, SimpleFsNodeKind kind);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] InodeNumber inode() const noexcept;
    [[nodiscard]] SimpleFsNodeKind kind() const noexcept;
    [[nodiscard]] bool is_file() const noexcept;
    [[nodiscard]] bool is_directory() const noexcept;

private:
    std::string name_;
    InodeNumber inode_ = InodeNumber::invalid();
    SimpleFsNodeKind kind_ = SimpleFsNodeKind::COUNT;
};

class SimpleFileSystem final
{
public:
    static void format(block::BufferCache& cache, SimpleFsFormatOptions options = SimpleFsFormatOptions{});

    explicit SimpleFileSystem(block::BufferCache& cache);
    ~SimpleFileSystem();

    SimpleFileSystem(const SimpleFileSystem&) = delete;
    SimpleFileSystem& operator=(const SimpleFileSystem&) = delete;
    SimpleFileSystem(SimpleFileSystem&&) = delete;
    SimpleFileSystem& operator=(SimpleFileSystem&&) = delete;

    [[nodiscard]] InodeNumber root_inode() const noexcept;
    [[nodiscard]] SimpleFsInodeMetadata metadata(InodeNumber inode) const;
    [[nodiscard]] std::optional<SimpleFsDirectoryEntry> lookup(InodeNumber directory, std::string_view name) const;
    [[nodiscard]] std::vector<SimpleFsDirectoryEntry> read_directory(InodeNumber directory) const;

    [[nodiscard]] InodeNumber create_file(InodeNumber parent_directory, std::string_view name);
    [[nodiscard]] InodeNumber create_directory(InodeNumber parent_directory, std::string_view name);
    [[nodiscard]] std::size_t read_file(InodeNumber file, std::uint64_t offset, std::span<cpu::Byte> destination) const;
    [[nodiscard]] std::size_t write_file(InodeNumber file, std::uint64_t offset, std::span<const cpu::Byte> source);

    void flush();

private:
    struct InodeRecord;

    [[nodiscard]] InodeRecord read_inode(InodeNumber inode) const;
    void write_inode(const InodeRecord& inode);
    [[nodiscard]] InodeNumber allocate_inode(SimpleFsNodeKind kind);
    [[nodiscard]] block::BlockAddress allocate_data_block();
    [[nodiscard]] std::uint64_t free_data_block_count() const;
    [[nodiscard]] std::uint64_t additional_blocks_needed(const InodeRecord& inode, std::uint64_t end_size) const;
    [[nodiscard]] std::size_t read_inode_bytes(
        const InodeRecord& inode,
        std::uint64_t offset,
        std::span<cpu::Byte> destination) const;
    [[nodiscard]] std::size_t write_inode_bytes(
        InodeRecord& inode,
        std::uint64_t offset,
        std::span<const cpu::Byte> source);
    void append_directory_entry(InodeRecord& directory, const SimpleFsDirectoryEntry& entry);
    void validate_inode_number(InodeNumber inode) const;
    void validate_directory_name(std::string_view name) const;
    void validate_directory_inode(const InodeRecord& inode) const;
    void validate_file_inode(const InodeRecord& inode) const;
    [[nodiscard]] std::uint64_t max_file_size_bytes() const noexcept;

    block::BufferCache& cache_;
    std::size_t block_size_bytes_ = std::size_t{0};
    std::uint64_t block_count_ = std::uint64_t{0};
    std::uint64_t inode_table_start_ = std::uint64_t{0};
    std::uint32_t inode_table_block_count_ = std::uint32_t{0};
    std::uint32_t inode_count_ = std::uint32_t{0};
    std::uint64_t data_bitmap_start_ = std::uint64_t{0};
    std::uint32_t data_bitmap_block_count_ = std::uint32_t{0};
    std::uint64_t data_start_ = std::uint64_t{0};
    std::uint64_t data_block_count_ = std::uint64_t{0};
    InodeNumber root_inode_ = InodeNumber::root();
};
}
