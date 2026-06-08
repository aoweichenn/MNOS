#include <algorithm>
#include <limits>
#include <stdexcept>

#include <mnos/os/block/buffer_cache.hpp>

namespace
{
constexpr const char* BUFFER_CACHE_INVALID_CAPACITY_MESSAGE = "buffer cache capacity must be non-zero";
constexpr const char* BUFFER_CACHE_STORAGE_OVERFLOW_MESSAGE = "buffer cache storage capacity overflows size_t";
constexpr const char* BUFFER_CACHE_BLOCK_SPAN_MESSAGE = "buffer cache I/O requires exactly one block";
constexpr const char* BUFFER_CACHE_BLOCK_RANGE_MESSAGE = "buffer cache block address is outside the device";

[[nodiscard]] std::size_t checked_cache_storage_size(
    const std::size_t capacity_blocks,
    const std::size_t block_size_bytes)
{
    if (capacity_blocks == std::size_t{0})
    {
        throw std::invalid_argument{BUFFER_CACHE_INVALID_CAPACITY_MESSAGE};
    }
    if (block_size_bytes != std::size_t{0} &&
        capacity_blocks > std::numeric_limits<std::size_t>::max() / block_size_bytes)
    {
        throw std::overflow_error{BUFFER_CACHE_STORAGE_OVERFLOW_MESSAGE};
    }
    return capacity_blocks * block_size_bytes;
}
}

namespace mnos::os::block
{
std::uint64_t BufferCacheStats::hit_count() const noexcept
{
    return this->hit_count_;
}

std::uint64_t BufferCacheStats::miss_count() const noexcept
{
    return this->miss_count_;
}

std::uint64_t BufferCacheStats::device_read_count() const noexcept
{
    return this->device_read_count_;
}

std::uint64_t BufferCacheStats::write_count() const noexcept
{
    return this->write_count_;
}

std::uint64_t BufferCacheStats::device_writeback_count() const noexcept
{
    return this->device_writeback_count_;
}

std::uint64_t BufferCacheStats::dirty_eviction_count() const noexcept
{
    return this->dirty_eviction_count_;
}

void BufferCacheStats::record_hit() noexcept
{
    ++this->hit_count_;
}

void BufferCacheStats::record_miss() noexcept
{
    ++this->miss_count_;
}

void BufferCacheStats::record_device_read() noexcept
{
    ++this->device_read_count_;
}

void BufferCacheStats::record_write() noexcept
{
    ++this->write_count_;
}

void BufferCacheStats::record_device_writeback() noexcept
{
    ++this->device_writeback_count_;
}

void BufferCacheStats::record_dirty_eviction() noexcept
{
    ++this->dirty_eviction_count_;
}

BufferCache::BufferCache(MemoryBlockDevice& device, const std::size_t capacity_blocks) :
    device_(device),
    entries_(capacity_blocks),
    block_storage_(checked_cache_storage_size(capacity_blocks, device.block_size_bytes()), cpu::Byte{0})
{
    this->index_by_block_.reserve(capacity_blocks);
}

const BlockDeviceGeometry& BufferCache::geometry() const noexcept
{
    return this->device_.geometry();
}

std::size_t BufferCache::capacity_blocks() const noexcept
{
    return this->entries_.size();
}

std::size_t BufferCache::block_size_bytes() const noexcept
{
    return this->device_.block_size_bytes();
}

std::size_t BufferCache::valid_count() const noexcept
{
    return this->valid_count_;
}

std::size_t BufferCache::dirty_count() const noexcept
{
    std::size_t count = std::size_t{0};
    for (const Entry& entry : this->entries_)
    {
        if (entry.valid && entry.dirty)
        {
            ++count;
        }
    }
    return count;
}

bool BufferCache::contains(const BlockAddress address) const noexcept
{
    return this->index_by_block_.find(address.value()) != this->index_by_block_.end();
}

bool BufferCache::dirty(const BlockAddress address) const noexcept
{
    const auto found = this->index_by_block_.find(address.value());
    return found != this->index_by_block_.end() && this->entries_[found->second].dirty;
}

const BufferCacheStats& BufferCache::stats() const noexcept
{
    return this->stats_;
}

void BufferCache::read_block(const BlockAddress address, const std::span<cpu::Byte> destination)
{
    this->validate_block_span(destination.size());
    this->validate_device_address(address);
    const std::size_t entry_index = this->find_or_allocate_for_read(address);
    const std::span<const cpu::Byte> source = this->block_span(entry_index);
    std::copy_n(source.begin(), source.size(), destination.begin());
}

void BufferCache::write_block(const BlockAddress address, const std::span<const cpu::Byte> source)
{
    this->validate_block_span(source.size());
    this->validate_device_address(address);
    this->stats_.record_write();

    const std::size_t entry_index = this->find_or_allocate_for_write(address);
    std::span<cpu::Byte> destination = this->block_span(entry_index);
    std::copy_n(source.begin(), source.size(), destination.begin());
    this->entries_[entry_index].dirty = true;
}

void BufferCache::flush_block(const BlockAddress address)
{
    this->validate_device_address(address);
    const auto found = this->index_by_block_.find(address.value());
    if (found == this->index_by_block_.end())
    {
        return;
    }
    this->flush_entry(found->second);
}

void BufferCache::flush_all()
{
    for (std::size_t entry_index = std::size_t{0}; entry_index < this->entries_.size(); ++entry_index)
    {
        this->flush_entry(entry_index);
    }
}

void BufferCache::clear()
{
    this->flush_all();
    for (Entry& entry : this->entries_)
    {
        entry = Entry{};
    }
    this->index_by_block_.clear();
    this->access_tick_ = std::uint64_t{0};
    this->valid_count_ = std::size_t{0};
}

std::span<cpu::Byte> BufferCache::block_span(const std::size_t entry_index) noexcept
{
    const std::size_t offset = entry_index * this->block_size_bytes();
    return std::span<cpu::Byte>{
        this->block_storage_.data() + static_cast<std::ptrdiff_t>(offset),
        this->block_size_bytes()};
}

std::span<const cpu::Byte> BufferCache::block_span(const std::size_t entry_index) const noexcept
{
    const std::size_t offset = entry_index * this->block_size_bytes();
    return std::span<const cpu::Byte>{
        this->block_storage_.data() + static_cast<std::ptrdiff_t>(offset),
        this->block_size_bytes()};
}

std::size_t BufferCache::find_or_allocate_for_read(const BlockAddress address)
{
    const auto found = this->index_by_block_.find(address.value());
    if (found != this->index_by_block_.end())
    {
        this->stats_.record_hit();
        this->touch(found->second);
        return found->second;
    }

    this->stats_.record_miss();
    const std::size_t entry_index = this->allocate_entry(address);
    std::span<cpu::Byte> destination = this->block_span(entry_index);
    this->device_.read_block(address, destination);
    this->stats_.record_device_read();
    this->touch(entry_index);
    return entry_index;
}

std::size_t BufferCache::find_or_allocate_for_write(const BlockAddress address)
{
    const auto found = this->index_by_block_.find(address.value());
    if (found != this->index_by_block_.end())
    {
        this->stats_.record_hit();
        this->touch(found->second);
        return found->second;
    }

    this->stats_.record_miss();
    const std::size_t entry_index = this->allocate_entry(address);
    this->touch(entry_index);
    return entry_index;
}

std::size_t BufferCache::allocate_entry(const BlockAddress address)
{
    const std::size_t entry_index = this->victim_index();
    Entry& entry = this->entries_[entry_index];
    this->evict_if_needed(entry_index);
    if (!entry.valid)
    {
        ++this->valid_count_;
    }

    entry.address = address;
    entry.valid = true;
    entry.dirty = false;
    this->index_by_block_[address.value()] = entry_index;
    return entry_index;
}

std::size_t BufferCache::victim_index() const noexcept
{
    for (std::size_t entry_index = std::size_t{0}; entry_index < this->entries_.size(); ++entry_index)
    {
        if (!this->entries_[entry_index].valid)
        {
            return entry_index;
        }
    }

    std::size_t victim = std::size_t{0};
    for (std::size_t entry_index = std::size_t{1}; entry_index < this->entries_.size(); ++entry_index)
    {
        if (this->entries_[entry_index].last_used_tick < this->entries_[victim].last_used_tick)
        {
            victim = entry_index;
        }
    }
    return victim;
}

void BufferCache::evict_if_needed(const std::size_t entry_index)
{
    Entry& entry = this->entries_[entry_index];
    if (!entry.valid)
    {
        return;
    }

    if (entry.dirty)
    {
        this->stats_.record_dirty_eviction();
        this->flush_entry(entry_index);
    }
    this->index_by_block_.erase(entry.address.value());
}

void BufferCache::touch(const std::size_t entry_index) noexcept
{
    ++this->access_tick_;
    this->entries_[entry_index].last_used_tick = this->access_tick_;
}

void BufferCache::validate_block_span(const std::size_t byte_count) const
{
    if (byte_count != this->block_size_bytes())
    {
        throw std::invalid_argument{BUFFER_CACHE_BLOCK_SPAN_MESSAGE};
    }
}

void BufferCache::validate_device_address(const BlockAddress address) const
{
    if (!this->device_.contains(address))
    {
        throw std::out_of_range{BUFFER_CACHE_BLOCK_RANGE_MESSAGE};
    }
}

void BufferCache::flush_entry(const std::size_t entry_index)
{
    Entry& entry = this->entries_[entry_index];
    if (!entry.valid || !entry.dirty)
    {
        return;
    }

    const std::span<const cpu::Byte> source = this->block_span(entry_index);
    this->device_.write_block(entry.address, source);
    entry.dirty = false;
    this->stats_.record_device_writeback();
}
}
