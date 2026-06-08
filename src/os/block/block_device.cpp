#include <algorithm>
#include <limits>
#include <stdexcept>

#include <mnos/os/block/block_device.hpp>

namespace
{
constexpr const char* BLOCK_DEVICE_INVALID_GEOMETRY_MESSAGE =
    "block device geometry requires non-zero block size and block count";
constexpr const char* BLOCK_DEVICE_CAPACITY_OVERFLOW_MESSAGE = "block device geometry capacity overflows uint64";
constexpr const char* BLOCK_DEVICE_STORAGE_TOO_LARGE_MESSAGE = "memory block device capacity exceeds addressable storage";
constexpr const char* BLOCK_DEVICE_BLOCK_SPAN_MESSAGE = "block device I/O requires whole blocks";
constexpr const char* BLOCK_DEVICE_RANGE_MESSAGE = "block device block range is outside the device";

[[nodiscard]] std::uint64_t checked_capacity_bytes(
    const std::size_t block_size_bytes,
    const std::uint64_t block_count)
{
    if (block_size_bytes == std::size_t{0} || block_count == std::uint64_t{0})
    {
        throw std::invalid_argument{BLOCK_DEVICE_INVALID_GEOMETRY_MESSAGE};
    }

    const std::uint64_t block_size = static_cast<std::uint64_t>(block_size_bytes);
    if (block_count > std::numeric_limits<std::uint64_t>::max() / block_size)
    {
        throw std::overflow_error{BLOCK_DEVICE_CAPACITY_OVERFLOW_MESSAGE};
    }

    return block_size * block_count;
}

[[nodiscard]] std::size_t storage_size_for_capacity(const std::uint64_t capacity_bytes)
{
    if (capacity_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::length_error{BLOCK_DEVICE_STORAGE_TOO_LARGE_MESSAGE};
    }
    return static_cast<std::size_t>(capacity_bytes);
}
}

namespace mnos::os::block
{
BlockDeviceGeometry::BlockDeviceGeometry(
    const std::size_t block_size_bytes,
    const std::uint64_t block_count) :
    block_size_bytes_(block_size_bytes),
    block_count_(block_count),
    capacity_bytes_(checked_capacity_bytes(block_size_bytes, block_count))
{
}

std::size_t BlockDeviceGeometry::block_size_bytes() const noexcept
{
    return this->block_size_bytes_;
}

std::uint64_t BlockDeviceGeometry::block_count() const noexcept
{
    return this->block_count_;
}

std::uint64_t BlockDeviceGeometry::capacity_bytes() const noexcept
{
    return this->capacity_bytes_;
}

MemoryBlockDevice::MemoryBlockDevice(BlockDeviceGeometry geometry) :
    geometry_(geometry),
    storage_(storage_size_for_capacity(geometry.capacity_bytes()), cpu::Byte{0})
{
}

const BlockDeviceGeometry& MemoryBlockDevice::geometry() const noexcept
{
    return this->geometry_;
}

std::size_t MemoryBlockDevice::block_size_bytes() const noexcept
{
    return this->geometry_.block_size_bytes();
}

std::uint64_t MemoryBlockDevice::block_count() const noexcept
{
    return this->geometry_.block_count();
}

std::uint64_t MemoryBlockDevice::capacity_bytes() const noexcept
{
    return this->geometry_.capacity_bytes();
}

bool MemoryBlockDevice::contains(const BlockAddress address) const noexcept
{
    return address.value() < this->geometry_.block_count();
}

void MemoryBlockDevice::read_block(const BlockAddress address, const std::span<cpu::Byte> destination) const
{
    this->validate_block_span(destination.size());
    this->validate_block_range(address, std::uint64_t{1});

    const std::size_t offset = this->byte_offset_for(address);
    std::copy_n(this->storage_.begin() + static_cast<std::ptrdiff_t>(offset), destination.size(), destination.begin());
}

void MemoryBlockDevice::write_block(const BlockAddress address, const std::span<const cpu::Byte> source)
{
    this->validate_block_span(source.size());
    this->validate_block_range(address, std::uint64_t{1});

    const std::size_t offset = this->byte_offset_for(address);
    std::copy_n(source.begin(), source.size(), this->storage_.begin() + static_cast<std::ptrdiff_t>(offset));
}

void MemoryBlockDevice::read_blocks(const BlockAddress first_address, const std::span<cpu::Byte> destination) const
{
    const std::uint64_t requested_block_count = this->span_block_count(destination.size());
    this->validate_block_range(first_address, requested_block_count);
    if (requested_block_count == std::uint64_t{0})
    {
        return;
    }

    const std::size_t offset = this->byte_offset_for(first_address);
    std::copy_n(this->storage_.begin() + static_cast<std::ptrdiff_t>(offset), destination.size(), destination.begin());
}

void MemoryBlockDevice::write_blocks(const BlockAddress first_address, const std::span<const cpu::Byte> source)
{
    const std::uint64_t requested_block_count = this->span_block_count(source.size());
    this->validate_block_range(first_address, requested_block_count);
    if (requested_block_count == std::uint64_t{0})
    {
        return;
    }

    const std::size_t offset = this->byte_offset_for(first_address);
    std::copy_n(source.begin(), source.size(), this->storage_.begin() + static_cast<std::ptrdiff_t>(offset));
}

void MemoryBlockDevice::clear(const cpu::Byte fill_value) noexcept
{
    std::fill(this->storage_.begin(), this->storage_.end(), fill_value);
}

std::size_t MemoryBlockDevice::byte_offset_for(const BlockAddress address) const
{
    const std::uint64_t offset =
        address.value() * static_cast<std::uint64_t>(this->geometry_.block_size_bytes());
    return static_cast<std::size_t>(offset);
}

std::uint64_t MemoryBlockDevice::span_block_count(const std::size_t byte_count) const
{
    if (byte_count % this->geometry_.block_size_bytes() != std::size_t{0})
    {
        throw std::invalid_argument{BLOCK_DEVICE_BLOCK_SPAN_MESSAGE};
    }
    return static_cast<std::uint64_t>(byte_count / this->geometry_.block_size_bytes());
}

void MemoryBlockDevice::validate_block_span(const std::size_t byte_count) const
{
    if (byte_count != this->geometry_.block_size_bytes())
    {
        throw std::invalid_argument{BLOCK_DEVICE_BLOCK_SPAN_MESSAGE};
    }
}

void MemoryBlockDevice::validate_block_range(
    const BlockAddress first_address,
    const std::uint64_t requested_block_count) const
{
    if (requested_block_count == std::uint64_t{0})
    {
        if (first_address.value() > this->geometry_.block_count())
        {
            throw std::out_of_range{BLOCK_DEVICE_RANGE_MESSAGE};
        }
        return;
    }

    if (first_address.value() >= this->geometry_.block_count())
    {
        throw std::out_of_range{BLOCK_DEVICE_RANGE_MESSAGE};
    }
    const std::uint64_t remaining_block_count = this->geometry_.block_count() - first_address.value();
    if (requested_block_count > remaining_block_count)
    {
        throw std::out_of_range{BLOCK_DEVICE_RANGE_MESSAGE};
    }
}
}
