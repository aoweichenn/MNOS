#include <mnos/cpu/perf/performance_model.hpp>

namespace
{
[[nodiscard]] mnos::cpu::CycleCount to_cycle_count(const std::size_t value) noexcept
{
    return static_cast<mnos::cpu::CycleCount>(value);
}
}

namespace mnos::cpu::perf
{
void PerformanceCounters::reset() noexcept
{
    *this = PerformanceCounters{};
}

CycleCount PerformanceCounters::cycles() const noexcept
{
    return this->cycles_;
}

CycleCount PerformanceCounters::instructions_retired() const noexcept
{
    return this->instructions_retired_;
}

CycleCount PerformanceCounters::pipeline_flushes() const noexcept
{
    return this->pipeline_flushes_;
}

CycleCount PerformanceCounters::pipeline_stall_cycles() const noexcept
{
    return this->pipeline_stall_cycles_;
}

CycleCount PerformanceCounters::instruction_fetches() const noexcept
{
    return this->instruction_fetches_;
}

CycleCount PerformanceCounters::data_reads() const noexcept
{
    return this->data_reads_;
}

CycleCount PerformanceCounters::data_writes() const noexcept
{
    return this->data_writes_;
}

CycleCount PerformanceCounters::l1i_hits() const noexcept
{
    return this->l1i_hits_;
}

CycleCount PerformanceCounters::l1i_misses() const noexcept
{
    return this->l1i_misses_;
}

CycleCount PerformanceCounters::l1d_hits() const noexcept
{
    return this->l1d_hits_;
}

CycleCount PerformanceCounters::l1d_misses() const noexcept
{
    return this->l1d_misses_;
}

CycleCount PerformanceCounters::dirty_evictions() const noexcept
{
    return this->dirty_evictions_;
}

CycleCount PerformanceCounters::tlb_hits() const noexcept
{
    return this->tlb_hits_;
}

CycleCount PerformanceCounters::tlb_misses() const noexcept
{
    return this->tlb_misses_;
}

CycleCount PerformanceCounters::branch_instructions() const noexcept
{
    return this->branch_instructions_;
}

CycleCount PerformanceCounters::branch_redirects() const noexcept
{
    return this->branch_redirects_;
}

void PerformanceCounters::add_cycles(const CycleCount cycles) noexcept
{
    this->cycles_ += cycles;
}

void PerformanceCounters::retire_instruction() noexcept
{
    ++this->instructions_retired_;
}

void PerformanceCounters::record_pipeline_flush() noexcept
{
    ++this->pipeline_flushes_;
}

void PerformanceCounters::add_pipeline_stall_cycles(const CycleCount cycles) noexcept
{
    this->pipeline_stall_cycles_ += cycles;
}

void PerformanceCounters::record_instruction_fetch(const memory::CacheAccessResult& result) noexcept
{
    ++this->instruction_fetches_;
    this->l1i_hits_ += to_cycle_count(result.hit_count());
    this->l1i_misses_ += to_cycle_count(result.miss_count());
    this->dirty_evictions_ += to_cycle_count(result.dirty_eviction_count());
}

void PerformanceCounters::record_data_read(const memory::CacheAccessResult& result) noexcept
{
    ++this->data_reads_;
    this->l1d_hits_ += to_cycle_count(result.hit_count());
    this->l1d_misses_ += to_cycle_count(result.miss_count());
    this->dirty_evictions_ += to_cycle_count(result.dirty_eviction_count());
}

void PerformanceCounters::record_data_write(const memory::CacheAccessResult& result) noexcept
{
    ++this->data_writes_;
    this->l1d_hits_ += to_cycle_count(result.hit_count());
    this->l1d_misses_ += to_cycle_count(result.miss_count());
    this->dirty_evictions_ += to_cycle_count(result.dirty_eviction_count());
}

void PerformanceCounters::record_tlb_hit() noexcept
{
    ++this->tlb_hits_;
}

void PerformanceCounters::record_tlb_miss() noexcept
{
    ++this->tlb_misses_;
}

void PerformanceCounters::record_branch(const bool redirected) noexcept
{
    ++this->branch_instructions_;
    if (redirected)
    {
        ++this->branch_redirects_;
    }
}

Stage8PerformanceConfig::Stage8PerformanceConfig(
    memory::CacheGeometry instruction_cache_geometry,
    memory::CacheGeometry data_cache_geometry,
    const memory::CacheWritePolicy data_cache_write_policy,
    execution::PipelineConfig pipeline_config,
    const CycleCount cache_hit_cycles,
    const CycleCount cache_miss_penalty_cycles,
    const CycleCount cache_dirty_eviction_penalty_cycles,
    const CycleCount tlb_miss_penalty_cycles) noexcept :
    instruction_cache_geometry_(instruction_cache_geometry),
    data_cache_geometry_(data_cache_geometry),
    data_cache_write_policy_(data_cache_write_policy),
    pipeline_config_(pipeline_config),
    cache_hit_cycles_(cache_hit_cycles),
    cache_miss_penalty_cycles_(cache_miss_penalty_cycles),
    cache_dirty_eviction_penalty_cycles_(cache_dirty_eviction_penalty_cycles),
    tlb_miss_penalty_cycles_(tlb_miss_penalty_cycles)
{
}

const memory::CacheGeometry& Stage8PerformanceConfig::instruction_cache_geometry() const noexcept
{
    return this->instruction_cache_geometry_;
}

const memory::CacheGeometry& Stage8PerformanceConfig::data_cache_geometry() const noexcept
{
    return this->data_cache_geometry_;
}

memory::CacheWritePolicy Stage8PerformanceConfig::data_cache_write_policy() const noexcept
{
    return this->data_cache_write_policy_;
}

const execution::PipelineConfig& Stage8PerformanceConfig::pipeline_config() const noexcept
{
    return this->pipeline_config_;
}

CycleCount Stage8PerformanceConfig::cache_hit_cycles() const noexcept
{
    return this->cache_hit_cycles_;
}

CycleCount Stage8PerformanceConfig::cache_miss_penalty_cycles() const noexcept
{
    return this->cache_miss_penalty_cycles_;
}

CycleCount Stage8PerformanceConfig::cache_dirty_eviction_penalty_cycles() const noexcept
{
    return this->cache_dirty_eviction_penalty_cycles_;
}

CycleCount Stage8PerformanceConfig::tlb_miss_penalty_cycles() const noexcept
{
    return this->tlb_miss_penalty_cycles_;
}

Stage8PerformanceModel::Stage8PerformanceModel(Stage8PerformanceConfig config) :
    config_(config),
    instruction_cache_(config.instruction_cache_geometry(), memory::CacheWritePolicy::WRITE_THROUGH),
    data_cache_(config.data_cache_geometry(), config.data_cache_write_policy()),
    pipeline_(config.pipeline_config())
{
}

const Stage8PerformanceConfig& Stage8PerformanceModel::config() const noexcept
{
    return this->config_;
}

PerformanceCounters& Stage8PerformanceModel::counters() noexcept
{
    return this->counters_;
}

const PerformanceCounters& Stage8PerformanceModel::counters() const noexcept
{
    return this->counters_;
}

memory::SetAssociativeCache& Stage8PerformanceModel::instruction_cache() noexcept
{
    return this->instruction_cache_;
}

const memory::SetAssociativeCache& Stage8PerformanceModel::instruction_cache() const noexcept
{
    return this->instruction_cache_;
}

memory::SetAssociativeCache& Stage8PerformanceModel::data_cache() noexcept
{
    return this->data_cache_;
}

const memory::SetAssociativeCache& Stage8PerformanceModel::data_cache() const noexcept
{
    return this->data_cache_;
}

execution::InOrderPipeline& Stage8PerformanceModel::pipeline() noexcept
{
    return this->pipeline_;
}

const execution::InOrderPipeline& Stage8PerformanceModel::pipeline() const noexcept
{
    return this->pipeline_;
}

void Stage8PerformanceModel::reset() noexcept
{
    this->counters_.reset();
    this->instruction_cache_.clear();
    this->data_cache_.clear();
    this->pipeline_.reset();
}

void Stage8PerformanceModel::record_instruction_fetch(const Address64 rip, const std::size_t byte_count)
{
    const memory::CacheAccessResult result =
        this->instruction_cache_.access(rip, byte_count, memory::CacheAccessKind::EXECUTE);
    this->counters_.record_instruction_fetch(result);
    this->add_cache_cycles(result);
}

void Stage8PerformanceModel::record_data_read(const Address64 physical_address, const std::size_t byte_count)
{
    const memory::CacheAccessResult result =
        this->data_cache_.access(physical_address, byte_count, memory::CacheAccessKind::READ);
    this->counters_.record_data_read(result);
    this->add_cache_cycles(result);
}

void Stage8PerformanceModel::record_data_write(const Address64 physical_address, const std::size_t byte_count)
{
    const memory::CacheAccessResult result =
        this->data_cache_.access(physical_address, byte_count, memory::CacheAccessKind::WRITE);
    this->counters_.record_data_write(result);
    this->add_cache_cycles(result);
}

void Stage8PerformanceModel::record_tlb_hit() noexcept
{
    this->counters_.record_tlb_hit();
}

void Stage8PerformanceModel::record_tlb_miss() noexcept
{
    this->counters_.record_tlb_miss();
    this->counters_.add_pipeline_stall_cycles(this->config_.tlb_miss_penalty_cycles());
    this->counters_.add_cycles(this->config_.tlb_miss_penalty_cycles());
}

void Stage8PerformanceModel::record_retired_instruction(
    const bool control_flow_instruction,
    const bool redirected) noexcept
{
    const execution::PipelineStepResult result = this->pipeline_.retire(control_flow_instruction, redirected);
    this->counters_.retire_instruction();
    this->counters_.add_cycles(result.total_cycles());
    this->counters_.add_pipeline_stall_cycles(result.stall_cycles());
    if (result.flushed())
    {
        this->counters_.record_pipeline_flush();
    }
    if (control_flow_instruction)
    {
        this->counters_.record_branch(redirected);
    }
}

void Stage8PerformanceModel::record_exception_flush() noexcept
{
    const execution::PipelineStepResult result = this->pipeline_.flush_for_exception();
    this->counters_.add_cycles(result.total_cycles());
    this->counters_.add_pipeline_stall_cycles(result.stall_cycles());
    this->counters_.record_pipeline_flush();
}

void Stage8PerformanceModel::add_cache_cycles(const memory::CacheAccessResult& result) noexcept
{
    const CycleCount hit_cycles = to_cycle_count(result.hit_count()) * this->config_.cache_hit_cycles();
    const CycleCount miss_cycles = to_cycle_count(result.miss_count()) *
        (this->config_.cache_hit_cycles() + this->config_.cache_miss_penalty_cycles());
    const CycleCount dirty_eviction_cycles =
        to_cycle_count(result.dirty_eviction_count()) * this->config_.cache_dirty_eviction_penalty_cycles();
    const CycleCount stall_cycles =
        to_cycle_count(result.miss_count()) * this->config_.cache_miss_penalty_cycles() + dirty_eviction_cycles;
    this->counters_.add_cycles(hit_cycles + miss_cycles + dirty_eviction_cycles);
    this->counters_.add_pipeline_stall_cycles(stall_cycles);
}
}
