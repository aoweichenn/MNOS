#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/os/block/block_device.hpp>

namespace mnos::os::block
{
inline constexpr std::size_t BUFFER_CACHE_DEFAULT_CAPACITY_BLOCKS = 64;

class BufferCacheStats final
{
public:
    [[nodiscard]] std::uint64_t hit_count() const noexcept;
    [[nodiscard]] std::uint64_t miss_count() const noexcept;
    [[nodiscard]] std::uint64_t device_read_count() const noexcept;
    [[nodiscard]] std::uint64_t write_count() const noexcept;
    [[nodiscard]] std::uint64_t device_writeback_count() const noexcept;
    [[nodiscard]] std::uint64_t dirty_eviction_count() const noexcept;

private:
    friend class BufferCache;

    void record_hit() noexcept;
    void record_miss() noexcept;
    void record_device_read() noexcept;
    void record_write() noexcept;
    void record_device_writeback() noexcept;
    void record_dirty_eviction() noexcept;

    std::uint64_t hit_count_ = std::uint64_t{0};
    std::uint64_t miss_count_ = std::uint64_t{0};
    std::uint64_t device_read_count_ = std::uint64_t{0};
    std::uint64_t write_count_ = std::uint64_t{0};
    std::uint64_t device_writeback_count_ = std::uint64_t{0};
    std::uint64_t dirty_eviction_count_ = std::uint64_t{0};
};

class BufferCache final
{
public:
    explicit BufferCache(MemoryBlockDevice& device, std::size_t capacity_blocks = BUFFER_CACHE_DEFAULT_CAPACITY_BLOCKS);

    [[nodiscard]] const BlockDeviceGeometry& geometry() const noexcept;
    [[nodiscard]] std::size_t capacity_blocks() const noexcept;
    [[nodiscard]] std::size_t block_size_bytes() const noexcept;
    [[nodiscard]] std::size_t valid_count() const noexcept;
    [[nodiscard]] std::size_t dirty_count() const noexcept;
    [[nodiscard]] bool contains(BlockAddress address) const noexcept;
    [[nodiscard]] bool dirty(BlockAddress address) const noexcept;
    [[nodiscard]] const BufferCacheStats& stats() const noexcept;

    void read_block(BlockAddress address, std::span<cpu::Byte> destination);
    void write_block(BlockAddress address, std::span<const cpu::Byte> source);
    void flush_block(BlockAddress address);
    void flush_all();
    void clear();

private:
    struct Entry
    {
        BlockAddress address;
        std::uint64_t last_used_tick = std::uint64_t{0};
        bool valid = false;
        bool dirty = false;
    };

    [[nodiscard]] std::span<cpu::Byte> block_span(std::size_t entry_index) noexcept;
    [[nodiscard]] std::span<const cpu::Byte> block_span(std::size_t entry_index) const noexcept;
    [[nodiscard]] std::size_t find_or_allocate_for_read(BlockAddress address);
    [[nodiscard]] std::size_t find_or_allocate_for_write(BlockAddress address);
    [[nodiscard]] std::size_t allocate_entry(BlockAddress address);
    [[nodiscard]] std::size_t victim_index() const noexcept;
    void evict_if_needed(std::size_t entry_index);
    void touch(std::size_t entry_index) noexcept;
    void validate_block_span(std::size_t byte_count) const;
    void validate_device_address(BlockAddress address) const;
    void flush_entry(std::size_t entry_index);

    MemoryBlockDevice& device_;
    std::vector<Entry> entries_;
    std::vector<cpu::Byte> block_storage_;
    std::unordered_map<BlockAddress::value_type, std::size_t> index_by_block_;
    BufferCacheStats stats_;
    std::uint64_t access_tick_ = std::uint64_t{0};
    std::size_t valid_count_ = std::size_t{0};
};
}
