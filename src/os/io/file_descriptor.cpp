#include <limits>

#include <mnos/os/io/file_descriptor.hpp>

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

FileDescriptorTable::FileDescriptorTable() noexcept :
    entries_{
        FileDescriptorEntry{FileDescriptor::stdin(), FileDeviceKind::TTY, FileAccessMode::READ_ONLY},
        FileDescriptorEntry{FileDescriptor::stdout(), FileDeviceKind::TTY, FileAccessMode::WRITE_ONLY},
        FileDescriptorEntry{FileDescriptor::stderr(), FileDeviceKind::TTY, FileAccessMode::WRITE_ONLY}}
{
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

FileDescriptor file_descriptor_from_raw(const std::uint64_t raw_value) noexcept
{
    if (raw_value > static_cast<std::uint64_t>(std::numeric_limits<FileDescriptor::value_type>::max()))
    {
        return FileDescriptor::invalid();
    }
    return FileDescriptor{static_cast<FileDescriptor::value_type>(raw_value)};
}
}
