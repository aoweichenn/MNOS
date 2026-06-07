#pragma once

#include <mnos/cpu/common/types.hpp>

namespace mnos::cpu::execution
{
inline constexpr CycleCount PIPELINE_DEFAULT_RETIRE_CYCLES = CycleCount{1};
inline constexpr CycleCount PIPELINE_DEFAULT_BRANCH_REDIRECT_CYCLES = CycleCount{4};
inline constexpr CycleCount PIPELINE_DEFAULT_EXCEPTION_FLUSH_CYCLES = CycleCount{5};

class PipelineConfig final
{
public:
    PipelineConfig() noexcept = default;
    PipelineConfig(
        CycleCount retire_cycles,
        CycleCount branch_redirect_cycles,
        CycleCount exception_flush_cycles) noexcept;

    [[nodiscard]] CycleCount retire_cycles() const noexcept;
    [[nodiscard]] CycleCount branch_redirect_cycles() const noexcept;
    [[nodiscard]] CycleCount exception_flush_cycles() const noexcept;

private:
    CycleCount retire_cycles_ = PIPELINE_DEFAULT_RETIRE_CYCLES;
    CycleCount branch_redirect_cycles_ = PIPELINE_DEFAULT_BRANCH_REDIRECT_CYCLES;
    CycleCount exception_flush_cycles_ = PIPELINE_DEFAULT_EXCEPTION_FLUSH_CYCLES;
};

class PipelineStepResult final
{
public:
    PipelineStepResult() noexcept = default;
    PipelineStepResult(CycleCount base_cycles, CycleCount stall_cycles, bool flushed) noexcept;

    [[nodiscard]] CycleCount base_cycles() const noexcept;
    [[nodiscard]] CycleCount stall_cycles() const noexcept;
    [[nodiscard]] CycleCount total_cycles() const noexcept;
    [[nodiscard]] bool flushed() const noexcept;

private:
    CycleCount base_cycles_ = CycleCount{0};
    CycleCount stall_cycles_ = CycleCount{0};
    bool flushed_ = false;
};

class InOrderPipeline final
{
public:
    explicit InOrderPipeline(PipelineConfig config = PipelineConfig{}) noexcept;

    [[nodiscard]] const PipelineConfig& config() const noexcept;
    [[nodiscard]] CycleCount retired_instruction_count() const noexcept;
    [[nodiscard]] CycleCount flush_count() const noexcept;

    void reset() noexcept;
    [[nodiscard]] PipelineStepResult retire(bool control_flow_instruction, bool redirected) noexcept;
    [[nodiscard]] PipelineStepResult flush_for_exception() noexcept;

private:
    PipelineConfig config_;
    CycleCount retired_instruction_count_ = CycleCount{0};
    CycleCount flush_count_ = CycleCount{0};
};
}
