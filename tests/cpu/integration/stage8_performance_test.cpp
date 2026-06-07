#include <cstddef>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/memory/cache.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/perf/performance_model.hpp>
#include <mnos/cpu/register/id.hpp>

namespace cpu = mnos::cpu;
namespace cpu_execution = mnos::cpu::execution;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_perf = mnos::cpu::perf;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 256;
constexpr std::size_t TEST_PROGRAM_STEP_COUNT = 8;
constexpr cpu::Address64 TEST_STORED_ADDRESS = cpu::Address64{72};
constexpr cpu::Qword TEST_EXPECTED_VALUE = cpu::Qword{42};

[[nodiscard]] cpu_perf::Stage8PerformanceConfig make_stage8_test_config()
{
    return cpu_perf::Stage8PerformanceConfig{
        cpu_memory::CacheGeometry{16, 4, 2},
        cpu_memory::CacheGeometry{16, 4, 2},
        cpu_memory::CacheWritePolicy::WRITE_BACK,
        cpu_execution::PipelineConfig{cpu::CycleCount{1}, cpu::CycleCount{4}, cpu::CycleCount{5}},
        cpu::CycleCount{1},
        cpu::CycleCount{9},
        cpu::CycleCount{7},
        cpu::CycleCount{5}};
}

[[nodiscard]] cpu::ExecutableImage make_stage8_branching_image()
{
    return cpu::ExecutableImage{
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 1
        0x48, 0x81, 0xC0, 0x29, 0x00, 0x00, 0x00,                   // ADD RAX, 41
        0x48, 0x89, 0x45, 0x08,                                     // MOV [RBP + 8], RAX
        0x48, 0x8B, 0x5D, 0x08,                                     // MOV RBX, [RBP + 8]
        0x48, 0x81, 0xFB, 0x2A, 0x00, 0x00, 0x00,                   // CMP RBX, 42
        0x74, 0x0A,                                                 // JE +10
        0x48, 0xB9, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RCX, 13
        0xF4};                                                      // HLT
}
}

TEST(Stage8ExecutorIntegrationTest, CollectsCachePipelineAndBranchCountersWithoutChangingResults)
{
    const cpu::ExecutableImage image = make_stage8_branching_image();
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;
    executor.enable_stage8_performance_model(make_stage8_test_config());

    const std::size_t executed_steps = executor.run(state, image, memory_bus, image.size());

    EXPECT_THAT(executed_steps, Eq(TEST_PROGRAM_STEP_COUNT));
    EXPECT_THAT(executor.cycle_count(), Eq(static_cast<cpu::CycleCount>(TEST_PROGRAM_STEP_COUNT)));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(memory.read_qword(TEST_STORED_ADDRESS), Eq(TEST_EXPECTED_VALUE));

    const cpu_perf::PerformanceCounters& counters = executor.stage8_performance_model().counters();
    EXPECT_THAT(counters.instructions_retired(), Eq(static_cast<cpu::CycleCount>(TEST_PROGRAM_STEP_COUNT)));
    EXPECT_THAT(counters.instruction_fetches(), Eq(static_cast<cpu::CycleCount>(TEST_PROGRAM_STEP_COUNT)));
    EXPECT_GT(counters.l1i_misses(), cpu::CycleCount{0});
    EXPECT_GT(counters.l1i_hits(), cpu::CycleCount{0});
    EXPECT_THAT(counters.data_writes(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.data_reads(), Eq(cpu::CycleCount{1}));
    EXPECT_GT(counters.l1d_misses(), cpu::CycleCount{0});
    EXPECT_GT(counters.branch_instructions(), cpu::CycleCount{0});
    EXPECT_THAT(counters.branch_redirects(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.pipeline_flushes(), Eq(cpu::CycleCount{1}));
    EXPECT_GT(counters.cycles(), executor.cycle_count());
}

TEST(Stage8ExecutorIntegrationTest, CanResetAndDisablePerformanceModel)
{
    const cpu::ExecutableImage image{0xF4}; // HLT
    cpu::CpuState state;
    cpu::Executor executor;
    executor.enable_stage8_performance_model(make_stage8_test_config());
    static_cast<void>(executor.run(state, image));
    EXPECT_GT(executor.stage8_performance_model().counters().cycles(), cpu::CycleCount{0});

    executor.reset();
    EXPECT_THAT(executor.cycle_count(), Eq(cpu::CycleCount{0}));
    EXPECT_THAT(executor.stage8_performance_model().counters().cycles(), Eq(cpu::CycleCount{0}));

    executor.disable_stage8_performance_model();
    EXPECT_FALSE(executor.has_stage8_performance_model());
    EXPECT_FALSE(executor.mmu().has_stage8_performance_model());
    EXPECT_THROW(static_cast<void>(executor.stage8_performance_model()), std::logic_error);
}
