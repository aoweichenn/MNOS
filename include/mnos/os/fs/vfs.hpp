#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/os/fs/simple_fs.hpp>

namespace mnos::os::fs
{
enum class VfsOpenMode : std::uint8_t
{
    READ_ONLY,
    WRITE_ONLY,
    READ_WRITE,
    COUNT
};

class VfsNode final
{
public:
    VfsNode() noexcept = default;
    VfsNode(InodeNumber inode, SimpleFsNodeKind kind, std::uint64_t size_bytes) noexcept;

    [[nodiscard]] InodeNumber inode() const noexcept;
    [[nodiscard]] SimpleFsNodeKind kind() const noexcept;
    [[nodiscard]] std::uint64_t size_bytes() const noexcept;
    [[nodiscard]] bool is_file() const noexcept;
    [[nodiscard]] bool is_directory() const noexcept;

private:
    InodeNumber inode_ = InodeNumber::invalid();
    SimpleFsNodeKind kind_ = SimpleFsNodeKind::COUNT;
    std::uint64_t size_bytes_ = std::uint64_t{0};
};

class VfsFile final
{
public:
    VfsFile() noexcept = default;
    VfsFile(SimpleFileSystem& file_system, InodeNumber inode, VfsOpenMode mode) noexcept;

    [[nodiscard]] InodeNumber inode() const noexcept;
    [[nodiscard]] VfsOpenMode mode() const noexcept;
    [[nodiscard]] std::uint64_t offset() const noexcept;
    [[nodiscard]] std::uint64_t size_bytes() const;
    [[nodiscard]] bool readable() const noexcept;
    [[nodiscard]] bool writable() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    void seek(std::uint64_t offset);
    [[nodiscard]] std::size_t read(std::span<cpu::Byte> destination);
    [[nodiscard]] std::size_t write(std::span<const cpu::Byte> source);

private:
    SimpleFileSystem* file_system_ = nullptr;
    InodeNumber inode_ = InodeNumber::invalid();
    VfsOpenMode mode_ = VfsOpenMode::COUNT;
    std::uint64_t offset_ = std::uint64_t{0};
};

class Vfs final
{
public:
    explicit Vfs(SimpleFileSystem& file_system) noexcept;

    [[nodiscard]] VfsNode root() const;
    [[nodiscard]] std::optional<VfsNode> lookup(std::string_view path) const;
    [[nodiscard]] VfsNode create_file(std::string_view path);
    [[nodiscard]] VfsNode create_directory(std::string_view path);
    [[nodiscard]] VfsFile open_file(std::string_view path, VfsOpenMode mode);
    [[nodiscard]] std::vector<SimpleFsDirectoryEntry> read_directory(std::string_view path) const;

private:
    struct ParentPath
    {
        InodeNumber parent = InodeNumber::invalid();
        std::string leaf;
    };

    [[nodiscard]] std::vector<std::string> split_absolute_path(std::string_view path) const;
    [[nodiscard]] ParentPath resolve_parent(std::string_view path) const;
    [[nodiscard]] VfsNode node_from_inode(InodeNumber inode) const;

    SimpleFileSystem& file_system_;
};
}
