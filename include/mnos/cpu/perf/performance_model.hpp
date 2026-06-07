#pragma once

#include <cstddef>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/execution/pipeline.hpp>
#include <mnos/cpu/memory/cache.hpp>

namespace mnos::cpu::perf
{
inline constexpr CycleCount PERF_DEFAULT_CACHE_HIT_CYCLES = CycleCount{1};
inline constexpr CycleCount PERF_DEFAULT_CACHE_MISS_PENALTY_CYCLES = CycleCount{12};
inline constexpr CycleCount PERF_DEFAULT_CACHE_DIRTY_EVICTION_PENALTY_CYCLES = CycleCount{8};
inline constexpr CycleCount PERF_DEFAULT_TLB_MISS_PENALTY_CYCLES = CycleCount{7};

class PerformanceCounters final
{
public:
    void reset() noexcept;

    [[nodiscard]] CycleCount cycles() const noexcept;
    [[nodiscard]] CycleCount instructions_retired() const noexcept;
    [[nodiscard]] CycleCount pipeline_flushes() const noexcept;
    [[nodiscard]] CycleCount pipeline_stall_cycles() const noexcept;
    [[nodiscard]] CycleCount instruction_fetches() const noexcept;
    [[nodiscard]] CycleCount data_reads() const noexcept;
    [[nodiscard]] CycleCount data_writes() const noexcept;
    [[nodiscard]] CycleCount l1i_hits() const noexcept;
    [[nodiscard]] CycleCount l1i_misses() const noexcept;
    [[nodiscard]] CycleCount l1d_hits() const noexcept;
    [[nodiscard]] CycleCount l1d_misses() const noexcept;
    [[nodiscard]] CycleCount dirty_evictions() const noexcept;
    [[nodiscard]] CycleCount tlb_hits() const noexcept;
    [[nodiscard]] CycleCount tlb_misses() const noexcept;
    [[nodiscard]] CycleCount branch_instructions() const noexcept;
    [[nodiscard]] CycleCount branch_redirects() const noexcept;

    void add_cycles(CycleCount cycles) noexcept;
    void retire_instruction() noexcept;
    void record_pipeline_flush() noexcept;
    void add_pipeline_stall_cycles(CycleCount cycles) noexcept;
    void record_instruction_fetch(const memory::CacheAccessResult& result) noexcept;
    void record_data_read(const memory::CacheAccessResult& result) noexcept;
    void record_data_write(const memory::CacheAccessResult& result) noexcept;
    void record_tlb_hit() noexcept;
    void record_tlb_miss() noexcept;
    void record_branch(bool redirected) noexcept;

private:
    CycleCount cycles_ = CycleCount{0};
    CycleCount instructions_retired_ = CycleCount{0};
    CycleCount pipeline_flushes_ = CycleCount{0};
    CycleCount pipeline_stall_cycles_ = CycleCount{0};
    CycleCount instruction_fetches_ = CycleCount{0};
    CycleCount data_reads_ = CycleCount{0};
    CycleCount data_writes_ = CycleCount{0};
    CycleCount l1i_hits_ = CycleCount{0};
    CycleCount l1i_misses_ = CycleCount{0};
    CycleCount l1d_hits_ = CycleCount{0};
    CycleCount l1d_misses_ = CycleCount{0};
    CycleCount dirty_evictions_ = CycleCount{0};
    CycleCount tlb_hits_ = CycleCount{0};
    CycleCount tlb_misses_ = CycleCount{0};
    CycleCount branch_instructions_ = CycleCount{0};
    CycleCount branch_redirects_ = CycleCount{0};
};

class Stage8PerformanceConfig final
{
public:
    Stage8PerformanceConfig() noexcept = default;
    Stage8PerformanceConfig(
        memory::CacheGeometry instruction_cache_geometry,
        memory::CacheGeometry data_cache_geometry,
        memory::CacheWritePolicy data_cache_write_policy,
        execution::PipelineConfig pipeline_config,
        CycleCount cache_hit_cycles,
        CycleCount cache_miss_penalty_cycles,
        CycleCount cache_dirty_eviction_penalty_cycles,
        CycleCount tlb_miss_penalty_cycles) noexcept;

    [[nodiscard]] const memory::CacheGeometry& instruction_cache_geometry() const noexcept;
    [[nodiscard]] const memory::CacheGeometry& data_cache_geometry() const noexcept;
    [[nodiscard]] memory::CacheWritePolicy data_cache_write_policy() const noexcept;
    [[nodiscard]] const execution::PipelineConfig& pipeline_config() const noexcept;
    [[nodiscard]] CycleCount cache_hit_cycles() const noexcept;
    [[nodiscard]] CycleCount cache_miss_penalty_cycles() const noexcept;
    [[nodiscard]] CycleCount cache_dirty_eviction_penalty_cycles() const noexcept;
    [[nodiscard]] CycleCount tlb_miss_penalty_cycles() const noexcept;

private:
    memory::CacheGeometry instruction_cache_geometry_;
    memory::CacheGeometry data_cache_geometry_;
    memory::CacheWritePolicy data_cache_write_policy_ = memory::CacheWritePolicy::WRITE_BACK;
    execution::PipelineConfig pipeline_config_;
    CycleCount cache_hit_cycles_ = PERF_DEFAULT_CACHE_HIT_CYCLES;
    CycleCount cache_miss_penalty_cycles_ = PERF_DEFAULT_CACHE_MISS_PENALTY_CYCLES;
    CycleCount cache_dirty_eviction_penalty_cycles_ = PERF_DEFAULT_CACHE_DIRTY_EVICTION_PENALTY_CYCLES;
    CycleCount tlb_miss_penalty_cycles_ = PERF_DEFAULT_TLB_MISS_PENALTY_CYCLES;
};

class Stage8PerformanceModel final
{
public:
    explicit Stage8PerformanceModel(Stage8PerformanceConfig config = Stage8PerformanceConfig{});

    [[nodiscard]] const Stage8PerformanceConfig& config() const noexcept;
    [[nodiscard]] PerformanceCounters& counters() noexcept;
    [[nodiscard]] const PerformanceCounters& counters() const noexcept;
    [[nodiscard]] memory::SetAssociativeCache& instruction_cache() noexcept;
    [[nodiscard]] const memory::SetAssociativeCache& instruction_cache() const noexcept;
    [[nodiscard]] memory::SetAssociativeCache& data_cache() noexcept;
    [[nodiscard]] const memory::SetAssociativeCache& data_cache() const noexcept;
    [[nodiscard]] execution::InOrderPipeline& pipeline() noexcept;
    [[nodiscard]] const execution::InOrderPipeline& pipeline() const noexcept;

    void reset() noexcept;
    void record_instruction_fetch(Address64 rip, std::size_t byte_count);
    void record_data_read(Address64 physical_address, std::size_t byte_count);
    void record_data_write(Address64 physical_address, std::size_t byte_count);
    void record_tlb_hit() noexcept;
    void record_tlb_miss() noexcept;
    void record_retired_instruction(bool control_flow_instruction, bool redirected) noexcept;
    void record_exception_flush() noexcept;

private:
    void add_cache_cycles(const memory::CacheAccessResult& result) noexcept;

    Stage8PerformanceConfig config_;
    PerformanceCounters counters_;
    memory::SetAssociativeCache instruction_cache_;
    memory::SetAssociativeCache data_cache_;
    execution::InOrderPipeline pipeline_;
};
}
