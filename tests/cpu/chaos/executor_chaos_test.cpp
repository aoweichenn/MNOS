#include <array>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cpu/support/cpu_test_helpers.hpp>
#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/register/id.hpp>
#include <support/deterministic_prng.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;
namespace cpu_support = mnos::test::cpu_support;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t CHAOS_SEED = 0xC001CAFEULL;
constexpr std::size_t CHAOS_OPERATION_COUNT = 96;
constexpr std::size_t CHAOS_PROGRAM_EXTRA_INSTRUCTIONS = 3;
constexpr std::size_t CHAOS_PROGRAM_CAPACITY = CHAOS_OPERATION_COUNT + CHAOS_PROGRAM_EXTRA_INSTRUCTIONS;
constexpr std::size_t CHAOS_MEMORY_SIZE_BYTES = 512;
constexpr std::size_t CHAOS_MEMORY_SLOT_COUNT = 16;
constexpr cpu::ADDRESS64 CHAOS_MEMORY_BASE_ADDRESS = cpu::ADDRESS64{128};
constexpr cpu::SQWORD64 CHAOS_MAX_IMMEDIATE = cpu::SQWORD64{127};
constexpr cpu::SQWORD64 CHAOS_RBP_INITIAL_VALUE = static_cast<cpu::SQWORD64>(CHAOS_MEMORY_BASE_ADDRESS);
constexpr cpu::UQWORD64 CHAOS_INITIAL_RAX = cpu::UQWORD64{0};
constexpr cpu::UQWORD64 CHAOS_INITIAL_RBX = cpu::UQWORD64{0};

enum class ChaosOperation : std::uint8_t
{
    MOV_RAX_IMM,
    ADD_RAX_IMM,
    SUB_RAX_IMM,
    STORE_RAX,
    LOAD_RBX,
    COUNT
};

inline constexpr std::uint64_t CHAOS_OPERATION_KIND_COUNT = static_cast<std::uint64_t>(ChaosOperation::COUNT);

[[nodiscard]] cpu::SQWORD64 next_small_immediate(test::DeterministicPrng& prng) noexcept
{
    const auto raw_value = static_cast<cpu::SQWORD64>(prng.next_bounded(static_cast<std::uint64_t>(CHAOS_MAX_IMMEDIATE)));
    return raw_value - (CHAOS_MAX_IMMEDIATE / cpu::SQWORD64{2});
}

[[nodiscard]] std::size_t next_memory_slot(test::DeterministicPrng& prng) noexcept
{
    return static_cast<std::size_t>(prng.next_bounded(CHAOS_MEMORY_SLOT_COUNT));
}

[[nodiscard]] cpu::SQWORD64 slot_displacement(const std::size_t slot) noexcept
{
    return static_cast<cpu::SQWORD64>(slot * cpu::DATA_SIZE_QWORD_BYTES);
}
}

TEST(ExecutorChaosTest, RunsDeterministicLongProgramAgainstModel)
{
    test::DeterministicPrng prng{CHAOS_SEED};
    cpu::Program program;
    program.reserve(CHAOS_PROGRAM_CAPACITY);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, CHAOS_RBP_INITIAL_VALUE));

    std::array<cpu::UQWORD64, CHAOS_MEMORY_SLOT_COUNT> expected_memory{};
    cpu::UQWORD64 expected_rax = CHAOS_INITIAL_RAX;
    cpu::UQWORD64 expected_rbx = CHAOS_INITIAL_RBX;

    for (std::size_t operation_index = 0; operation_index < CHAOS_OPERATION_COUNT; ++operation_index)
    {
        const auto operation = static_cast<ChaosOperation>(prng.next_bounded(CHAOS_OPERATION_KIND_COUNT));
        switch (operation)
        {
        case ChaosOperation::MOV_RAX_IMM:
        {
            const cpu::SQWORD64 immediate = next_small_immediate(prng);
            expected_rax = static_cast<cpu::UQWORD64>(immediate);
            program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, immediate));
            break;
        }
        case ChaosOperation::ADD_RAX_IMM:
        {
            const cpu::SQWORD64 immediate = next_small_immediate(prng);
            expected_rax += static_cast<cpu::UQWORD64>(immediate);
            program.push_back(cpu_support::make_add_imm(cpu::RegisterId::RAX, immediate));
            break;
        }
        case ChaosOperation::SUB_RAX_IMM:
        {
            const cpu::SQWORD64 immediate = next_small_immediate(prng);
            expected_rax -= static_cast<cpu::UQWORD64>(immediate);
            program.push_back(cpu_support::make_sub_imm(cpu::RegisterId::RAX, immediate));
            break;
        }
        case ChaosOperation::STORE_RAX:
        {
            const std::size_t slot = next_memory_slot(prng);
            expected_memory[slot] = expected_rax;
            program.push_back(cpu::Instruction::make_mov(
                cpu_support::make_mem(cpu::RegisterId::RBP, slot_displacement(slot), cpu::DataSize::QWORD),
                cpu::Operand::reg(cpu::RegisterId::RAX)));
            break;
        }
        case ChaosOperation::LOAD_RBX:
        {
            const std::size_t slot = next_memory_slot(prng);
            expected_rbx = expected_memory[slot];
            program.push_back(cpu::Instruction::make_mov(
                cpu::Operand::reg(cpu::RegisterId::RBX),
                cpu_support::make_mem(cpu::RegisterId::RBP, slot_displacement(slot), cpu::DataSize::QWORD)));
            break;
        }
        case ChaosOperation::COUNT:
            throw std::logic_error{"chaos operation sentinel should not be generated"};
        }
    }

    program.push_back(cpu::Instruction::make_halt());

    cpu::PhysicalMemory memory(CHAOS_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;
    cpu::ExecutionTrace trace;
    trace.reserve(program.size());

    const std::size_t executed_steps = executor.run(state, program, memory_bus, program.size(), &trace);

    EXPECT_THAT(executed_steps, Eq(program.size()));
    EXPECT_THAT(trace.size(), Eq(program.size()));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(expected_rax));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(expected_rbx));

    for (std::size_t slot = 0; slot < CHAOS_MEMORY_SLOT_COUNT; ++slot)
    {
        const cpu::ADDRESS64 address = CHAOS_MEMORY_BASE_ADDRESS + static_cast<cpu::ADDRESS64>(slot_displacement(slot));
        EXPECT_THAT(memory.read_qword(address), Eq(expected_memory[slot]));
    }
}
