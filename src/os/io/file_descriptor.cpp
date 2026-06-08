#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <mnos/os/io/file_descriptor.hpp>

namespace
{
constexpr const char* OPEN_FILE_DESCRIPTION_MISSING_MESSAGE = "file descriptor entry has no open file description";
constexpr const char* OPEN_FILE_DESCRIPTION_EXPECTED_VFS_FILE_MESSAGE =
    "open file description does not contain a vfs file";
constexpr const char* FILE_DESCRIPTOR_ENTRY_EXPECTED_TTY_MESSAGE =
    "file descriptor entry requires a vfs file for non-tty devices";
constexpr const char* FILE_DESCRIPTOR_TABLE_EXHAUSTED_MESSAGE = "file descriptor table has no free descriptor";

[[nodiscard]] mnos::os::io::FileAccessMode access_mode_from_vfs_mode(
    const mnos::os::fs::VfsOpenMode mode) noexcept
{
    switch (mode)
    {
    case mnos::os::fs::VfsOpenMode::READ_ONLY:
        return mnos::os::io::FileAccessMode::READ_ONLY;
    case mnos::os::fs::VfsOpenMode::WRITE_ONLY:
        return mnos::os::io::FileAccessMode::WRITE_ONLY;
    case mnos::os::fs::VfsOpenMode::READ_WRITE:
        return mnos::os::io::FileAccessMode::READ_WRITE;
    case mnos::os::fs::VfsOpenMode::COUNT:
        break;
    }
    return mnos::os::io::FileAccessMode::COUNT;
}
}

namespace mnos::os::io
{
IoResult IoResult::ready(const std::size_t byte_count) noexcept
{
    return IoResult{IoStatus::READY, byte_count};
}

IoResult IoResult::blocked() noexcept
{
    return IoResult{IoStatus::BLOCKED, std::size_t{0}};
}

IoResult IoResult::bad_descriptor() noexcept
{
    return IoResult{IoStatus::BAD_DESCRIPTOR, std::size_t{0}};
}

IoResult IoResult::bad_address() noexcept
{
    return IoResult{IoStatus::BAD_ADDRESS, std::size_t{0}};
}

IoResult IoResult::invalid_argument() noexcept
{
    return IoResult{IoStatus::INVALID_ARGUMENT, std::size_t{0}};
}

IoResult IoResult::no_space() noexcept
{
    return IoResult{IoStatus::NO_SPACE, std::size_t{0}};
}

IoResult::IoResult(const IoStatus status, const std::size_t byte_count) noexcept :
    status_(status),
    byte_count_(byte_count)
{
}

IoStatus IoResult::status() const noexcept
{
    return this->status_;
}

std::size_t IoResult::byte_count() const noexcept
{
    return this->byte_count_;
}

bool IoResult::is_ready() const noexcept
{
    return this->status_ == IoStatus::READY;
}

bool IoResult::is_blocked() const noexcept
{
    return this->status_ == IoStatus::BLOCKED;
}

OpenFileDescription::OpenFileDescription(
    const FileDeviceKind device_kind,
    const FileAccessMode access_mode) noexcept :
    device_kind_(device_kind),
    access_mode_(access_mode)
{
}

OpenFileDescription OpenFileDescription::tty(const FileAccessMode access_mode) noexcept
{
    return OpenFileDescription{FileDeviceKind::TTY, access_mode};
}

OpenFileDescription OpenFileDescription::vfs_file(fs::VfsFile file)
{
    OpenFileDescription description{FileDeviceKind::VFS_FILE, access_mode_from_vfs_mode(file.mode())};
    description.vfs_file_.emplace(std::move(file));
    return description;
}

FileDeviceKind OpenFileDescription::device_kind() const noexcept
{
    return this->device_kind_;
}

FileAccessMode OpenFileDescription::access_mode() const noexcept
{
    return this->access_mode_;
}

bool OpenFileDescription::readable() const noexcept
{
    return this->access_mode_ == FileAccessMode::READ_ONLY || this->access_mode_ == FileAccessMode::READ_WRITE;
}

bool OpenFileDescription::writable() const noexcept
{
    return this->access_mode_ == FileAccessMode::WRITE_ONLY || this->access_mode_ == FileAccessMode::READ_WRITE;
}

bool OpenFileDescription::has_vfs_file() const noexcept
{
    return this->vfs_file_.has_value();
}

fs::VfsFile& OpenFileDescription::vfs_file()
{
    if (!this->vfs_file_.has_value())
    {
        throw std::logic_error{OPEN_FILE_DESCRIPTION_EXPECTED_VFS_FILE_MESSAGE};
    }
    return this->vfs_file_.value();
}

const fs::VfsFile& OpenFileDescription::vfs_file() const
{
    if (!this->vfs_file_.has_value())
    {
        throw std::logic_error{OPEN_FILE_DESCRIPTION_EXPECTED_VFS_FILE_MESSAGE};
    }
    return this->vfs_file_.value();
}

FileDescriptorEntry::FileDescriptorEntry(
    const FileDescriptor descriptor,
    const FileDeviceKind device_kind,
    const FileAccessMode access_mode) :
    descriptor_(descriptor)
{
    if (device_kind != FileDeviceKind::TTY)
    {
        throw std::invalid_argument{FILE_DESCRIPTOR_ENTRY_EXPECTED_TTY_MESSAGE};
    }
    this->description_ = std::make_shared<OpenFileDescription>(OpenFileDescription::tty(access_mode));
}

FileDescriptorEntry::FileDescriptorEntry(
    const FileDescriptor descriptor,
    std::shared_ptr<OpenFileDescription> description) :
    descriptor_(descriptor),
    description_(std::move(description))
{
}

FileDescriptor FileDescriptorEntry::descriptor() const noexcept
{
    return this->descriptor_;
}

FileDeviceKind FileDescriptorEntry::device_kind() const noexcept
{
    return this->description_ == nullptr ? FileDeviceKind::COUNT : this->description_->device_kind();
}

FileAccessMode FileDescriptorEntry::access_mode() const noexcept
{
    return this->description_ == nullptr ? FileAccessMode::COUNT : this->description_->access_mode();
}

bool FileDescriptorEntry::readable() const noexcept
{
    return this->description_ != nullptr && this->description_->readable();
}

bool FileDescriptorEntry::writable() const noexcept
{
    return this->description_ != nullptr && this->description_->writable();
}

bool FileDescriptorEntry::has_description() const noexcept
{
    return this->description_ != nullptr;
}

OpenFileDescription& FileDescriptorEntry::description()
{
    if (this->description_ == nullptr)
    {
        throw std::logic_error{OPEN_FILE_DESCRIPTION_MISSING_MESSAGE};
    }
    return *this->description_;
}

const OpenFileDescription& FileDescriptorEntry::description() const
{
    if (this->description_ == nullptr)
    {
        throw std::logic_error{OPEN_FILE_DESCRIPTION_MISSING_MESSAGE};
    }
    return *this->description_;
}

FileDescriptorTable::FileDescriptorTable()
{
    this->entries_.reserve(FILE_DESCRIPTOR_STANDARD_STREAM_COUNT);
    this->entries_.emplace_back(FileDescriptor::stdin(), FileDeviceKind::TTY, FileAccessMode::READ_ONLY);
    this->entries_.emplace_back(FileDescriptor::stdout(), FileDeviceKind::TTY, FileAccessMode::WRITE_ONLY);
    this->entries_.emplace_back(FileDescriptor::stderr(), FileDeviceKind::TTY, FileAccessMode::WRITE_ONLY);
}

const FileDescriptorEntry* FileDescriptorTable::find(const FileDescriptor descriptor) const noexcept
{
    for (const FileDescriptorEntry& entry : this->entries_)
    {
        if (entry.descriptor() == descriptor)
        {
            return &entry;
        }
    }
    return nullptr;
}

FileDescriptorEntry* FileDescriptorTable::find_mutable(const FileDescriptor descriptor) noexcept
{
    for (FileDescriptorEntry& entry : this->entries_)
    {
        if (entry.descriptor() == descriptor)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool FileDescriptorTable::contains(const FileDescriptor descriptor) const noexcept
{
    return this->find(descriptor) != nullptr;
}

bool FileDescriptorTable::readable(const FileDescriptor descriptor) const noexcept
{
    const FileDescriptorEntry* const entry = this->find(descriptor);
    return entry != nullptr && entry->readable();
}

bool FileDescriptorTable::writable(const FileDescriptor descriptor) const noexcept
{
    const FileDescriptorEntry* const entry = this->find(descriptor);
    return entry != nullptr && entry->writable();
}

std::size_t FileDescriptorTable::size() const noexcept
{
    return this->entries_.size();
}

FileDescriptor FileDescriptorTable::open_vfs_file(fs::VfsFile file)
{
    const FileDescriptor descriptor = this->next_available_descriptor();
    auto description = std::make_shared<OpenFileDescription>(OpenFileDescription::vfs_file(std::move(file)));
    this->entries_.emplace_back(descriptor, std::move(description));
    return descriptor;
}

bool FileDescriptorTable::close(const FileDescriptor descriptor) noexcept
{
    const auto entry = std::ranges::find_if(
        this->entries_,
        [descriptor](const FileDescriptorEntry& candidate) noexcept
        {
            return candidate.descriptor() == descriptor;
        });
    if (entry == this->entries_.end())
    {
        return false;
    }
    this->entries_.erase(entry);
    return true;
}

FileDescriptor FileDescriptorTable::next_available_descriptor() const
{
    for (FileDescriptor::value_type value = FILE_DESCRIPTOR_STDIN_VALUE;
         value < std::numeric_limits<FileDescriptor::value_type>::max();
         ++value)
    {
        const FileDescriptor descriptor{value};
        if (!this->contains(descriptor))
        {
            return descriptor;
        }
    }
    throw std::length_error{FILE_DESCRIPTOR_TABLE_EXHAUSTED_MESSAGE};
}

FileDescriptor file_descriptor_from_raw(const std::uint64_t raw_value) noexcept
{
    if (raw_value > static_cast<std::uint64_t>(std::numeric_limits<FileDescriptor::value_type>::max()))
    {
        return FileDescriptor::invalid();
    }
    return FileDescriptor{static_cast<FileDescriptor::value_type>(raw_value)};
}
}
