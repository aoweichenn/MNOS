#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

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
    COUNT
};

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
    constexpr FileDescriptorEntry() noexcept = default;
    constexpr FileDescriptorEntry(
        FileDescriptor descriptor,
        FileDeviceKind device_kind,
        FileAccessMode access_mode) noexcept :
        descriptor_(descriptor),
        device_kind_(device_kind),
        access_mode_(access_mode)
    {
    }

    [[nodiscard]] constexpr FileDescriptor descriptor() const noexcept
    {
        return this->descriptor_;
    }

    [[nodiscard]] constexpr FileDeviceKind device_kind() const noexcept
    {
        return this->device_kind_;
    }

    [[nodiscard]] constexpr FileAccessMode access_mode() const noexcept
    {
        return this->access_mode_;
    }

    [[nodiscard]] constexpr bool readable() const noexcept
    {
        return this->access_mode_ == FileAccessMode::READ_ONLY || this->access_mode_ == FileAccessMode::READ_WRITE;
    }

    [[nodiscard]] constexpr bool writable() const noexcept
    {
        return this->access_mode_ == FileAccessMode::WRITE_ONLY || this->access_mode_ == FileAccessMode::READ_WRITE;
    }

private:
    FileDescriptor descriptor_ = FileDescriptor::invalid();
    FileDeviceKind device_kind_ = FileDeviceKind::COUNT;
    FileAccessMode access_mode_ = FileAccessMode::COUNT;
};

class IoResult final
{
public:
    [[nodiscard]] static IoResult ready(std::size_t byte_count) noexcept;
    [[nodiscard]] static IoResult blocked() noexcept;
    [[nodiscard]] static IoResult bad_descriptor() noexcept;
    [[nodiscard]] static IoResult bad_address() noexcept;
    [[nodiscard]] static IoResult invalid_argument() noexcept;

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
    FileDescriptorTable() noexcept;

    [[nodiscard]] const FileDescriptorEntry* find(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] bool contains(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] bool readable(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] bool writable(FileDescriptor descriptor) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::array<FileDescriptorEntry, FILE_DESCRIPTOR_STANDARD_STREAM_COUNT> entries_;
};

[[nodiscard]] FileDescriptor file_descriptor_from_raw(std::uint64_t raw_value) noexcept;
}
