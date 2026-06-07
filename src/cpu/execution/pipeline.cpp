#include <mnos/cpu/execution/pipeline.hpp>

namespace mnos::cpu::execution
{
PipelineConfig::PipelineConfig(
    const CycleCount retire_cycles,
    const CycleCount branch_redirect_cycles,
    const CycleCount exception_flush_cycles) noexcept :
    retire_cycles_(retire_cycles),
    branch_redirect_cycles_(branch_redirect_cycles),
    exception_flush_cycles_(exception_flush_cycles)
{
}

CycleCount PipelineConfig::retire_cycles() const noexcept
{
    return this->retire_cycles_;
}

CycleCount PipelineConfig::branch_redirect_cycles() const noexcept
{
    return this->branch_redirect_cycles_;
}

CycleCount PipelineConfig::exception_flush_cycles() const noexcept
{
    return this->exception_flush_cycles_;
}

PipelineStepResult::PipelineStepResult(
    const CycleCount base_cycles,
    const CycleCount stall_cycles,
    const bool flushed) noexcept :
    base_cycles_(base_cycles),
    stall_cycles_(stall_cycles),
    flushed_(flushed)
{
}

CycleCount PipelineStepResult::base_cycles() const noexcept
{
    return this->base_cycles_;
}

CycleCount PipelineStepResult::stall_cycles() const noexcept
{
    return this->stall_cycles_;
}

CycleCount PipelineStepResult::total_cycles() const noexcept
{
    return this->base_cycles_ + this->stall_cycles_;
}

bool PipelineStepResult::flushed() const noexcept
{
    return this->flushed_;
}

InOrderPipeline::InOrderPipeline(PipelineConfig config) noexcept : config_(config)
{
}

const PipelineConfig& InOrderPipeline::config() const noexcept
{
    return this->config_;
}

CycleCount InOrderPipeline::retired_instruction_count() const noexcept
{
    return this->retired_instruction_count_;
}

CycleCount InOrderPipeline::flush_count() const noexcept
{
    return this->flush_count_;
}

void InOrderPipeline::reset() noexcept
{
    this->retired_instruction_count_ = CycleCount{0};
    this->flush_count_ = CycleCount{0};
}

PipelineStepResult InOrderPipeline::retire(
    const bool control_flow_instruction,
    const bool redirected) noexcept
{
    ++this->retired_instruction_count_;
    if (control_flow_instruction && redirected)
    {
        ++this->flush_count_;
        return PipelineStepResult{
            this->config_.retire_cycles(),
            this->config_.branch_redirect_cycles(),
            true};
    }

    return PipelineStepResult{this->config_.retire_cycles(), CycleCount{0}, false};
}

PipelineStepResult InOrderPipeline::flush_for_exception() noexcept
{
    ++this->flush_count_;
    return PipelineStepResult{CycleCount{0}, this->config_.exception_flush_cycles(), true};
}
}
