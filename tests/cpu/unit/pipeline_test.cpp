#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/execution/pipeline.hpp>

namespace cpu = mnos::cpu;
namespace cpu_execution = mnos::cpu::execution;

namespace
{
using ::testing::Eq;

constexpr cpu::CycleCount TEST_RETIRE_CYCLES = cpu::CycleCount{2};
constexpr cpu::CycleCount TEST_BRANCH_REDIRECT_CYCLES = cpu::CycleCount{6};
constexpr cpu::CycleCount TEST_EXCEPTION_FLUSH_CYCLES = cpu::CycleCount{9};
}

TEST(InOrderPipelineTest, RetiresInstructionsAndModelsRedirectFlushes)
{
    const cpu_execution::PipelineConfig config{
        TEST_RETIRE_CYCLES,
        TEST_BRANCH_REDIRECT_CYCLES,
        TEST_EXCEPTION_FLUSH_CYCLES};
    cpu_execution::InOrderPipeline pipeline{config};

    const cpu_execution::PipelineStepResult straight_line = pipeline.retire(false, false);
    EXPECT_THAT(straight_line.base_cycles(), Eq(TEST_RETIRE_CYCLES));
    EXPECT_THAT(straight_line.stall_cycles(), Eq(cpu::CycleCount{0}));
    EXPECT_THAT(straight_line.total_cycles(), Eq(TEST_RETIRE_CYCLES));
    EXPECT_FALSE(straight_line.flushed());
    EXPECT_THAT(pipeline.retired_instruction_count(), Eq(cpu::CycleCount{1}));

    const cpu_execution::PipelineStepResult redirected_branch = pipeline.retire(true, true);
    EXPECT_THAT(redirected_branch.base_cycles(), Eq(TEST_RETIRE_CYCLES));
    EXPECT_THAT(redirected_branch.stall_cycles(), Eq(TEST_BRANCH_REDIRECT_CYCLES));
    EXPECT_THAT(redirected_branch.total_cycles(), Eq(TEST_RETIRE_CYCLES + TEST_BRANCH_REDIRECT_CYCLES));
    EXPECT_TRUE(redirected_branch.flushed());
    EXPECT_THAT(pipeline.flush_count(), Eq(cpu::CycleCount{1}));
}

TEST(InOrderPipelineTest, FlushesExceptionsAndResetsRuntimeState)
{
    cpu_execution::InOrderPipeline pipeline{cpu_execution::PipelineConfig{
        TEST_RETIRE_CYCLES,
        TEST_BRANCH_REDIRECT_CYCLES,
        TEST_EXCEPTION_FLUSH_CYCLES}};

    static_cast<void>(pipeline.retire(true, true));
    const cpu_execution::PipelineStepResult exception_flush = pipeline.flush_for_exception();
    EXPECT_THAT(exception_flush.base_cycles(), Eq(cpu::CycleCount{0}));
    EXPECT_THAT(exception_flush.stall_cycles(), Eq(TEST_EXCEPTION_FLUSH_CYCLES));
    EXPECT_TRUE(exception_flush.flushed());
    EXPECT_THAT(pipeline.flush_count(), Eq(cpu::CycleCount{2}));

    pipeline.reset();
    EXPECT_THAT(pipeline.retired_instruction_count(), Eq(cpu::CycleCount{0}));
    EXPECT_THAT(pipeline.flush_count(), Eq(cpu::CycleCount{0}));
}
