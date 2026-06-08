#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <mnos/os/fs/vfs.hpp>

namespace mnos::os::io
{
inline constexpr std::int32_t FILE_DESCRIPTOR_INVALID_VALUE = std::int32_t{-1};
inline constexpr std::int32_t FILE_DESCRIPTOR_STDIN_VALUE = std::int32_t{0};
inline constexpr std::int32_t FILE_DESCRIPTOR_STDOUT_VALUE = std::int32_t{1};
inline constexpr std::int32_t FILE_DESCRIPTOR_STDERR_VALUE = std::int32_t{2};
inline constexpr std::size_t FILE_DESCRIPTOR_STANDARD_STREAM_COUNT = std::size_t{3};

enum class FileDeviceKind : std::uint8_t
{
    TTY,
    VFS_FILE,
    COUNT
};

enum class FileAccessMode : std::uint8_t
{
    READ_ONLY,
    WRITE_ONLY,
    READ_WRITE,
    COUNT
};

enum class IoStatus : std::uint8_t
{
    READY,
    BLOCKED,
    BAD_DESCRIPTOR,
    BAD_ADDRESS,
    INVALID_ARGUMENT,
    NO_SPACE,
    COUNT
};

class OpenFileDescription;

class FileDescriptor final
{
public:
    using value_type = std::int32_t;

    constexpr FileDescriptor() noexcept = default;
    explicit constexpr FileDescriptor(const value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] static constexpr FileDescriptor invalid() noexcept
    {
        return FileDescriptor{FILE_DESCRIPTOR_INVALID_VALUE};
    }

    [[nodiscard]] static constexpr FileDescriptor stdin() noexcept
    {
        return FileDescriptor{FILE_DESCRIPTOR_STDIN_VALUE};
    }

    [[nodiscard]] static constexpr FileDescriptor stdout() noexcept
    {
        return FileDescriptor{FILE_DESCRIPTOR_STDOUT_VALUE};
    }

    [[nodiscard]] static constexpr FileDescriptor stderr() noexcept
    {
        return FileDescriptor{FILE_DESCRIPTOR_STDERR_VALUE};
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return this->value_ != FILE_DESCRIPTOR_INVALID_VALUE;
    }

    [[nodiscard]] constexpr bool is_standard_stream() const noexcept
    {
        return this->value_ >= FILE_DESCRIPTOR_STDIN_VALUE && this->value_ <= FILE_DESCRIPTOR_STDERR_VALUE;
    }

    [[nodiscard]] friend constexpr auto operator<=>(const FileDescriptor&, const FileDescriptor&) noexcept = default;

private:
    value_type value_ = FILE_DESCRIPTOR_INVALID_VALUE;
};

class FileDescriptorEntry final
{
public:
    FileDescriptorEntry() noexcept = default;
    FileDescriptorEntry(
        FileDescriptor descriptor,
        FileDeviceKind device_kind,
        FileAccessMode access_mode);
    FileDescriptorEntry(FileDescriptor descriptor, std::shared_ptr<class OpenFileDescription> description);

    [[nodiscard]] FileDescriptor descriptor() const noexcept;
    [[nodiscard]] FileDeviceKind device_kind() const noexcept;
    [[nodiscard]] FileAccessMode access_mode() const noexcept;
    [[nodiscard]] bool readable() const noexcept;
    [[nodiscard]] bool writable() const noexcept;
    [[nodiscard]] bool has_description() const noexcept;
    [[nodiscard]] OpenFileDescription& description();
    [[nodiscard]] const OpenFileDescription& description() const;

private:
    FileDescriptor descriptor_ = FileDescriptor::invalid();
    std::shared_ptr<OpenFileDescription> description_;
};

class IoResult final
{
public:
    [[nodiscard]] static IoResult ready(std::size_t byte_count) noexcept;
    [[nodiscard]] static IoResult blocked() noexcept;
    [[nodiscard]] static IoResult bad_descriptor() noexcept;
    [[nodiscard]] static IoResult bad_address() noexcept;
    [[nodiscard]] static IoResult invalid_argument() noexcept;
    [[nodiscard]] static IoResult no_space() noexcept;

    [[nodiscard]] IoStatus status() const noexcept;
    [[nodiscard]] std::size_t byte_count() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_blocked() const noexcept;

private:
    IoResult(IoStatus status, std::size_t byte_count) noexcept;

    IoStatus status_;
    std::size_t byte_count_;
};

class FileDescriptorTable final
{
public:
    FileDescriptorTable();

    [[nodiscard]] const FileDescriptorEntry* find(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] FileDescriptorEntry* find_mutable(FileDescriptor descriptor) noexcept;
    [[nodiscard]] bool contains(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] bool readable(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] bool writable(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] FileDescriptor open_vfs_file(fs::VfsFile file);
    [[nodiscard]] bool close(FileDescriptor descriptor) noexcept;

private:
    [[nodiscard]] FileDescriptor next_available_descriptor() const;

    std::vector<FileDescriptorEntry> entries_;
};

class OpenFileDescription final
{
public:
    [[nodiscard]] static OpenFileDescription tty(FileAccessMode access_mode) noexcept;
    [[nodiscard]] static OpenFileDescription vfs_file(fs::VfsFile file);

    [[nodiscard]] FileDeviceKind device_kind() const noexcept;
    [[nodiscard]] FileAccessMode access_mode() const noexcept;
    [[nodiscard]] bool readable() const noexcept;
    [[nodiscard]] bool writable() const noexcept;
    [[nodiscard]] bool has_vfs_file() const noexcept;
    [[nodiscard]] fs::VfsFile& vfs_file();
    [[nodiscard]] const fs::VfsFile& vfs_file() const;

private:
    OpenFileDescription(FileDeviceKind device_kind, FileAccessMode access_mode) noexcept;

    FileDeviceKind device_kind_ = FileDeviceKind::COUNT;
    FileAccessMode access_mode_ = FileAccessMode::COUNT;
    std::optional<fs::VfsFile> vfs_file_;
};

[[nodiscard]] FileDescriptor file_descriptor_from_raw(std::uint64_t raw_value) noexcept;
}
