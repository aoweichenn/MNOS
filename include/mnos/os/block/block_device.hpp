#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <mnos/cpu/common/types.hpp>

namespace mnos::os::block
{
inline constexpr std::size_t BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES = 512;
inline constexpr std::uint64_t BLOCK_DEVICE_DEFAULT_BLOCK_COUNT = std::uint64_t{1024};

class BlockAddress final
{
public:
    using value_type = std::uint64_t;

    constexpr BlockAddress() noexcept = default;
    explicit constexpr BlockAddress(value_type value) noexcept : value_(value)
    {
    }

    [[nodiscard]] constexpr value_type value() const noexcept
    {
        return this->value_;
    }

    [[nodiscard]] friend constexpr auto operator<=>(const BlockAddress&, const BlockAddress&) noexcept = default;

private:
    value_type value_ = value_type{0};
};

class BlockDeviceGeometry final
{
public:
    BlockDeviceGeometry() noexcept = default;
    BlockDeviceGeometry(std::size_t block_size_bytes, std::uint64_t block_count);

    [[nodiscard]] std::size_t block_size_bytes() const noexcept;
    [[nodiscard]] std::uint64_t block_count() const noexcept;
    [[nodiscard]] std::uint64_t capacity_bytes() const noexcept;

private:
    std::size_t block_size_bytes_ = BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES;
    std::uint64_t block_count_ = BLOCK_DEVICE_DEFAULT_BLOCK_COUNT;
    std::uint64_t capacity_bytes_ =
        static_cast<std::uint64_t>(BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES) * BLOCK_DEVICE_DEFAULT_BLOCK_COUNT;
};

class MemoryBlockDevice final
{
public:
    explicit MemoryBlockDevice(BlockDeviceGeometry geometry = BlockDeviceGeometry{});

    [[nodiscard]] const BlockDeviceGeometry& geometry() const noexcept;
    [[nodiscard]] std::size_t block_size_bytes() const noexcept;
    [[nodiscard]] std::uint64_t block_count() const noexcept;
    [[nodiscard]] std::uint64_t capacity_bytes() const noexcept;
    [[nodiscard]] bool contains(BlockAddress address) const noexcept;

    void read_block(BlockAddress address, std::span<cpu::Byte> destination) const;
    void write_block(BlockAddress address, std::span<const cpu::Byte> source);
    void read_blocks(BlockAddress first_address, std::span<cpu::Byte> destination) const;
    void write_blocks(BlockAddress first_address, std::span<const cpu::Byte> source);
    void clear(cpu::Byte fill_value = cpu::Byte{0}) noexcept;

private:
    [[nodiscard]] std::size_t byte_offset_for(BlockAddress address) const;
    [[nodiscard]] std::uint64_t span_block_count(std::size_t byte_count) const;
    void validate_block_span(std::size_t byte_count) const;
    void validate_block_range(BlockAddress first_address, std::uint64_t requested_block_count) const;

    BlockDeviceGeometry geometry_;
    std::vector<cpu::Byte> storage_;
};
}
