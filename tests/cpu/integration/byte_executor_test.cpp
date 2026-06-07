#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/register/id.hpp>

namespace cpu = mnos::cpu;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr cpu::Address64 TEST_MEMORY_STORED_ADDRESS = cpu::Address64{72};
constexpr cpu::Address64 TEST_INDEXED_MEMORY_STORED_ADDRESS = cpu::Address64{80};
constexpr cpu::Address64 TEST_RIP_RELATIVE_LOAD_ADDRESS = cpu::Address64{16};
constexpr cpu::Qword TEST_EXPECTED_VALUE = cpu::Qword{42};
constexpr cpu::Qword TEST_SKIPPED_VALUE = cpu::Qword{13};
constexpr std::size_t TEST_MAIN_PROGRAM_STEP_COUNT = 8;
constexpr std::size_t TEST_RIP_RELATIVE_PROGRAM_STEP_COUNT = 2;
constexpr std::size_t TEST_INDEXED_PROGRAM_STEP_COUNT = 4;
}

TEST(ByteExecutorTest, RunsDecodedByteProgramThroughMemoryAndBranching)
{
    cpu::ExecutableImage image{
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 1
        0x48, 0x81, 0xC0, 0x29, 0x00, 0x00, 0x00,                   // ADD RAX, 41
        0x48, 0x89, 0x45, 0x08,                                     // MOV [RBP + 8], RAX
        0x48, 0x8B, 0x5D, 0x08,                                     // MOV RBX, [RBP + 8]
        0x48, 0x81, 0xFB, 0x2A, 0x00, 0x00, 0x00,                   // CMP RBX, 42
        0x74, 0x0A,                                                 // JE +10
        0x48, 0xB9, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RCX, 13
        0xF4};                                                      // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;
    cpu::ExecutionTrace trace;
    trace.reserve(TEST_MAIN_PROGRAM_STEP_COUNT);

    const std::size_t executed_steps = executor.run(state, image, memory_bus, image.size(), &trace);

    EXPECT_THAT(executed_steps, Eq(TEST_MAIN_PROGRAM_STEP_COUNT));
    EXPECT_THAT(trace.size(), Eq(TEST_MAIN_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.rip(), Eq(image.end_rip()));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RCX), Eq(cpu::Qword{0}));
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_STORED_ADDRESS), Eq(TEST_EXPECTED_VALUE));
}

TEST(ByteExecutorTest, ExecutesRipRelativeLoad)
{
    cpu::ExecutableImage image{
        0x48, 0x8B, 0x05, 0x09, 0x00, 0x00, 0x00, // MOV RAX, [RIP + 9]
        0xF4};                                    // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    memory.write_qword(TEST_RIP_RELATIVE_LOAD_ADDRESS, TEST_EXPECTED_VALUE);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_RIP_RELATIVE_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(TEST_EXPECTED_VALUE));
}

TEST(ByteExecutorTest, ExecutesSibIndexedLoad)
{
    cpu::ExecutableImage image{
        0x48, 0xB8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 2
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0x8B, 0x5C, 0x85, 0x08,                               // MOV RBX, [RBP + RAX*4 + 8]
        0xF4};                                                       // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    memory.write_qword(TEST_INDEXED_MEMORY_STORED_ADDRESS, TEST_SKIPPED_VALUE);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_INDEXED_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_SKIPPED_VALUE));
}

TEST(ByteExecutorTest, RejectsDecodedJumpOutsideImage)
{
    cpu::ExecutableImage image{0xEB, 0x7F};
    cpu::CpuState state;
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.step(state, image)), std::out_of_range);
}
