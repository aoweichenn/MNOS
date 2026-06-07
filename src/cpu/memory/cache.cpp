#include <algorithm>
#include <stdexcept>

#include <mnos/cpu/memory/cache.hpp>

namespace
{
constexpr const char* CACHE_INVALID_GEOMETRY_MESSAGE =
    "cache geometry requires non-zero power-of-two line size, set count, and associativity";

[[nodiscard]] bool is_power_of_two(const std::size_t value) noexcept
{
    return value != std::size_t{0} && (value & (value - std::size_t{1})) == std::size_t{0};
}

[[nodiscard]] std::size_t log2_power_of_two(const std::size_t value) noexcept
{
    return static_cast<std::size_t>(std::countr_zero(value));
}
}

namespace mnos::cpu::memory
{
CacheGeometry::CacheGeometry(
    const std::size_t line_size_bytes,
    const std::size_t set_count,
    const std::size_t associativity) :
    line_size_bytes_(line_size_bytes),
    set_count_(set_count),
    associativity_(associativity),
    line_size_shift_(log2_power_of_two(line_size_bytes)),
    set_count_shift_(log2_power_of_two(set_count)),
    set_index_mask_(set_count - std::size_t{1})
{
    if (!is_power_of_two(this->line_size_bytes_) || !is_power_of_two(this->set_count_) ||
        !is_power_of_two(this->associativity_))
    {
        throw std::invalid_argument{CACHE_INVALID_GEOMETRY_MESSAGE};
    }
}

std::size_t CacheGeometry::line_size_bytes() const noexcept
{
    return this->line_size_bytes_;
}

std::size_t CacheGeometry::set_count() const noexcept
{
    return this->set_count_;
}

std::size_t CacheGeometry::associativity() const noexcept
{
    return this->associativity_;
}

std::size_t CacheGeometry::line_count() const noexcept
{
    return this->set_count_ * this->associativity_;
}

std::size_t CacheGeometry::capacity_bytes() const noexcept
{
    return this->line_count() * this->line_size_bytes_;
}

std::size_t CacheGeometry::line_size_shift() const noexcept
{
    return this->line_size_shift_;
}

std::size_t CacheGeometry::set_count_shift() const noexcept
{
    return this->set_count_shift_;
}

std::size_t CacheGeometry::set_index_mask() const noexcept
{
    return this->set_index_mask_;
}

CacheAccessResult::CacheAccessResult(
    const std::size_t hit_count,
    const std::size_t miss_count,
    const std::size_t dirty_eviction_count) noexcept :
    hit_count_(hit_count),
    miss_count_(miss_count),
    dirty_eviction_count_(dirty_eviction_count)
{
}

std::size_t CacheAccessResult::hit_count() const noexcept
{
    return this->hit_count_;
}

std::size_t CacheAccessResult::miss_count() const noexcept
{
    return this->miss_count_;
}

std::size_t CacheAccessResult::dirty_eviction_count() const noexcept
{
    return this->dirty_eviction_count_;
}

bool CacheAccessResult::all_hits() const noexcept
{
    return this->touched_line_count() > std::size_t{0} && this->miss_count_ == std::size_t{0};
}

bool CacheAccessResult::any_miss() const noexcept
{
    return this->miss_count_ != std::size_t{0};
}

std::size_t CacheAccessResult::touched_line_count() const noexcept
{
    return this->hit_count_ + this->miss_count_;
}

void CacheAccessResult::add_hit() noexcept
{
    ++this->hit_count_;
}

void CacheAccessResult::add_miss() noexcept
{
    ++this->miss_count_;
}

void CacheAccessResult::add_dirty_eviction() noexcept
{
    ++this->dirty_eviction_count_;
}

void CacheAccessResult::merge(const CacheAccessResult& other) noexcept
{
    this->hit_count_ += other.hit_count_;
    this->miss_count_ += other.miss_count_;
    this->dirty_eviction_count_ += other.dirty_eviction_count_;
}

SetAssociativeCache::SetAssociativeCache(
    CacheGeometry geometry,
    const CacheWritePolicy write_policy) :
    geometry_(geometry),
    write_policy_(write_policy),
    lines_(geometry.line_count())
{
}

const CacheGeometry& SetAssociativeCache::geometry() const noexcept
{
    return this->geometry_;
}

CacheWritePolicy SetAssociativeCache::write_policy() const noexcept
{
    return this->write_policy_;
}

std::size_t SetAssociativeCache::valid_line_count() const noexcept
{
    return this->valid_line_count_;
}

bool SetAssociativeCache::empty() const noexcept
{
    return this->valid_line_count_ == std::size_t{0};
}

void SetAssociativeCache::clear() noexcept
{
    for (CacheLine& line : this->lines_)
    {
        line.valid = false;
        line.dirty = false;
        line.tag = Qword{0};
        line.last_used_tick = std::uint64_t{0};
    }
    this->valid_line_count_ = std::size_t{0};
    this->access_tick_ = std::uint64_t{0};
}

CacheAccessResult SetAssociativeCache::access(
    const Address64 address,
    const std::size_t byte_count,
    const CacheAccessKind kind)
{
    CacheAccessResult result;
    if (byte_count == std::size_t{0})
    {
        return result;
    }

    Address64 current_address = address;
    std::size_t remaining_bytes = byte_count;
    while (remaining_bytes > std::size_t{0})
    {
        const Address64 line_offset =
            current_address & static_cast<Address64>(this->geometry_.line_size_bytes() - std::size_t{1});
        const Address64 line_base_address = current_address - line_offset;
        result.merge(this->access_line(line_base_address, kind));

        const std::size_t bytes_until_next_line =
            this->geometry_.line_size_bytes() - static_cast<std::size_t>(line_offset);
        const std::size_t step = std::min(remaining_bytes, bytes_until_next_line);
        current_address += static_cast<Address64>(step);
        remaining_bytes -= step;
    }

    return result;
}

CacheAccessResult SetAssociativeCache::access_line(
    const Address64 line_base_address,
    const CacheAccessKind kind)
{
    CacheAccessResult result;
    const std::size_t set_index = this->set_index_for_line(line_base_address);
    const Qword tag = this->tag_for_line(line_base_address);
    const std::size_t start_index = this->set_start_index(set_index);
    const std::size_t end_index = start_index + this->geometry_.associativity();

    for (std::size_t line_index = start_index; line_index < end_index; ++line_index)
    {
        CacheLine& line = this->lines_[line_index];
        if (line.valid && line.tag == tag)
        {
            result.add_hit();
            this->touch_line(line, kind);
            return result;
        }
    }

    result.add_miss();
    this->fill_line(this->victim_index(set_index), tag, kind, result);
    return result;
}

std::size_t SetAssociativeCache::set_index_for_line(const Address64 line_base_address) const noexcept
{
    const Address64 line_number = line_base_address >> this->geometry_.line_size_shift();
    return static_cast<std::size_t>(line_number) & this->geometry_.set_index_mask();
}

Qword SetAssociativeCache::tag_for_line(const Address64 line_base_address) const noexcept
{
    const Address64 line_number = line_base_address >> this->geometry_.line_size_shift();
    return line_number >> this->geometry_.set_count_shift();
}

std::size_t SetAssociativeCache::set_start_index(const std::size_t set_index) const noexcept
{
    return set_index * this->geometry_.associativity();
}

std::size_t SetAssociativeCache::victim_index(const std::size_t set_index) const noexcept
{
    const std::size_t start_index = this->set_start_index(set_index);
    const std::size_t end_index = start_index + this->geometry_.associativity();
    for (std::size_t line_index = start_index; line_index < end_index; ++line_index)
    {
        if (!this->lines_[line_index].valid)
        {
            return line_index;
        }
    }

    std::size_t victim = start_index;
    for (std::size_t line_index = start_index + std::size_t{1}; line_index < end_index; ++line_index)
    {
        if (this->lines_[line_index].last_used_tick < this->lines_[victim].last_used_tick)
        {
            victim = line_index;
        }
    }
    return victim;
}

void SetAssociativeCache::fill_line(
    const std::size_t line_index,
    const Qword tag,
    const CacheAccessKind kind,
    CacheAccessResult& result) noexcept
{
    CacheLine& line = this->lines_[line_index];
    if (line.valid && line.dirty)
    {
        result.add_dirty_eviction();
    }
    if (!line.valid)
    {
        ++this->valid_line_count_;
    }

    line.valid = true;
    line.tag = tag;
    line.dirty = false;
    this->touch_line(line, kind);
}

void SetAssociativeCache::touch_line(CacheLine& line, const CacheAccessKind kind) noexcept
{
    ++this->access_tick_;
    line.last_used_tick = this->access_tick_;
    if (kind == CacheAccessKind::WRITE && this->write_policy_ == CacheWritePolicy::WRITE_BACK)
    {
        line.dirty = true;
    }
}
}
