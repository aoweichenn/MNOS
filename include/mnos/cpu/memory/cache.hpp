#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <mnos/cpu/common/types.hpp>

namespace mnos::cpu::memory
{
inline constexpr std::size_t CACHE_DEFAULT_LINE_SIZE_BYTES = 64;
inline constexpr std::size_t CACHE_DEFAULT_SET_COUNT = 64;
inline constexpr std::size_t CACHE_DEFAULT_ASSOCIATIVITY = 4;
inline constexpr std::size_t CACHE_DEFAULT_LINE_SIZE_SHIFT =
    static_cast<std::size_t>(std::countr_zero(CACHE_DEFAULT_LINE_SIZE_BYTES));
inline constexpr std::size_t CACHE_DEFAULT_SET_COUNT_SHIFT =
    static_cast<std::size_t>(std::countr_zero(CACHE_DEFAULT_SET_COUNT));
inline constexpr std::size_t CACHE_DEFAULT_SET_INDEX_MASK = CACHE_DEFAULT_SET_COUNT - std::size_t{1};

enum class CacheAccessKind : std::uint8_t
{
    READ,
    WRITE,
    EXECUTE,
};

enum class CacheWritePolicy : std::uint8_t
{
    WRITE_BACK,
    WRITE_THROUGH,
};

class CacheGeometry final
{
public:
    CacheGeometry() noexcept = default;
    CacheGeometry(std::size_t line_size_bytes, std::size_t set_count, std::size_t associativity);

    [[nodiscard]] std::size_t line_size_bytes() const noexcept;
    [[nodiscard]] std::size_t set_count() const noexcept;
    [[nodiscard]] std::size_t associativity() const noexcept;
    [[nodiscard]] std::size_t line_count() const noexcept;
    [[nodiscard]] std::size_t capacity_bytes() const noexcept;
    [[nodiscard]] std::size_t line_size_shift() const noexcept;
    [[nodiscard]] std::size_t set_count_shift() const noexcept;
    [[nodiscard]] std::size_t set_index_mask() const noexcept;

private:
    std::size_t line_size_bytes_ = CACHE_DEFAULT_LINE_SIZE_BYTES;
    std::size_t set_count_ = CACHE_DEFAULT_SET_COUNT;
    std::size_t associativity_ = CACHE_DEFAULT_ASSOCIATIVITY;
    std::size_t line_size_shift_ = CACHE_DEFAULT_LINE_SIZE_SHIFT;
    std::size_t set_count_shift_ = CACHE_DEFAULT_SET_COUNT_SHIFT;
    std::size_t set_index_mask_ = CACHE_DEFAULT_SET_INDEX_MASK;
};

class CacheAccessResult final
{
public:
    CacheAccessResult() noexcept = default;
    CacheAccessResult(std::size_t hit_count, std::size_t miss_count, std::size_t dirty_eviction_count) noexcept;

    [[nodiscard]] std::size_t hit_count() const noexcept;
    [[nodiscard]] std::size_t miss_count() const noexcept;
    [[nodiscard]] std::size_t dirty_eviction_count() const noexcept;
    [[nodiscard]] bool all_hits() const noexcept;
    [[nodiscard]] bool any_miss() const noexcept;
    [[nodiscard]] std::size_t touched_line_count() const noexcept;

    void add_hit() noexcept;
    void add_miss() noexcept;
    void add_dirty_eviction() noexcept;
    void merge(const CacheAccessResult& other) noexcept;

private:
    std::size_t hit_count_ = 0;
    std::size_t miss_count_ = 0;
    std::size_t dirty_eviction_count_ = 0;
};

class SetAssociativeCache final
{
public:
    explicit SetAssociativeCache(
        CacheGeometry geometry = CacheGeometry{},
        CacheWritePolicy write_policy = CacheWritePolicy::WRITE_BACK);

    [[nodiscard]] const CacheGeometry& geometry() const noexcept;
    [[nodiscard]] CacheWritePolicy write_policy() const noexcept;
    [[nodiscard]] std::size_t valid_line_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    void clear() noexcept;
    [[nodiscard]] CacheAccessResult access(Address64 address, std::size_t byte_count, CacheAccessKind kind);

private:
    struct CacheLine
    {
        Qword tag = Qword{0};
        std::uint64_t last_used_tick = std::uint64_t{0};
        bool valid = false;
        bool dirty = false;
    };

    [[nodiscard]] CacheAccessResult access_line(Address64 line_base_address, CacheAccessKind kind);
    [[nodiscard]] std::size_t set_index_for_line(Address64 line_base_address) const noexcept;
    [[nodiscard]] Qword tag_for_line(Address64 line_base_address) const noexcept;
    [[nodiscard]] std::size_t set_start_index(std::size_t set_index) const noexcept;
    [[nodiscard]] std::size_t victim_index(std::size_t set_index) const noexcept;
    void fill_line(std::size_t line_index, Qword tag, CacheAccessKind kind, CacheAccessResult& result) noexcept;
    void touch_line(CacheLine& line, CacheAccessKind kind) noexcept;

    CacheGeometry geometry_;
    CacheWritePolicy write_policy_;
    std::vector<CacheLine> lines_;
    std::uint64_t access_tick_ = std::uint64_t{0};
    std::size_t valid_line_count_ = 0;
};
}
