#include <stdexcept>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cpu/support/cpu_test_helpers.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;

namespace
{
using ::testing::Eq;

constexpr cpu::UQWORD64 TEST_REGISTER_VALUE = cpu::UQWORD64{0x1234ABCDULL};
constexpr cpu::SQWORD64 TEST_PROGRAM_INITIAL_VALUE = cpu::SQWORD64{1};
constexpr std::size_t TEST_PROGRAM_RESERVE_COUNT = 8;
constexpr std::size_t TEST_SINGLE_ENTRY_COUNT = 1;
constexpr std::size_t TEST_TWO_INSTRUCTION_COUNT = 2;
constexpr cpu::RIP64 TEST_FIRST_RIP = cpu::RIP64{0};
constexpr cpu::RIP64 TEST_SECOND_RIP = cpu::RIP64{1};
constexpr cpu::RIP64 TEST_TWO_INSTRUCTION_END_RIP = cpu::RIP64{2};
constexpr cpu::UQWORD64 TEST_TRACE_FIRST_CYCLE = cpu::UQWORD64{1};
constexpr bool TEST_TRACE_NOT_HALTED = false;
}

TEST(CpuStateTest, TracksRegistersFlagsRipAndHaltState)
{
    cpu::CpuState state;
    const cpu::CpuState& const_state = state;

    EXPECT_THAT(state.rip(), Eq(cpu::CPU_STATE_INITIAL_RIP));
    EXPECT_FALSE(state.is_halted());

    state.registers().write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    state.flags().write(cpu::FlagId::ZF, true);
    state.advance_rip();
    state.halt();

    EXPECT_THAT(const_state.registers().read(cpu::RegisterId::RAX), Eq(TEST_REGISTER_VALUE));
    EXPECT_TRUE(const_state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.rip(), Eq(cpu::CPU_STATE_NEXT_INSTRUCTION_OFFSET));
    EXPECT_TRUE(state.is_halted());

    state.resume();
    EXPECT_FALSE(state.is_halted());

    state.reset();
    EXPECT_THAT(state.rip(), Eq(cpu::CPU_STATE_INITIAL_RIP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(cpu::UQWORD64{0}));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::ZF));
}

TEST(ProgramTest, ProvidesStandardContainerAndRipAccess)
{
    cpu::Program program;
    EXPECT_TRUE(program.empty());

    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    EXPECT_FALSE(program.empty());
    EXPECT_THAT(program.size(), Eq(TEST_TWO_INSTRUCTION_COUNT));
    EXPECT_TRUE(program.contains_rip(TEST_FIRST_RIP));
    EXPECT_FALSE(program.contains_rip(TEST_TWO_INSTRUCTION_END_RIP));
    EXPECT_THAT(program.at(0).opcode(), Eq(cpu::Opcode::MOV));
    EXPECT_THAT(program.instruction_at(TEST_SECOND_RIP).opcode(), Eq(cpu::Opcode::HALT));
    EXPECT_THAT(program.instructions().size(), Eq(program.size()));
    EXPECT_THAT(program.begin()->opcode(), Eq(cpu::Opcode::MOV));

    auto program_iterator = program.begin();
    ++program_iterator;
    ++program_iterator;
    EXPECT_TRUE(program_iterator == program.end());
    EXPECT_THROW(static_cast<void>(program.instruction_at(TEST_TWO_INSTRUCTION_END_RIP)), std::out_of_range);

    program.clear();
    EXPECT_TRUE(program.empty());

    const cpu::Program initialized_program{
        cpu::Instruction::make_halt(),
    };
    EXPECT_THAT(initialized_program.size(), Eq(TEST_SINGLE_ENTRY_COUNT));

    std::vector<cpu::Instruction> raw_instructions;
    raw_instructions.push_back(cpu::Instruction::make_halt());
    const cpu::Program vector_program{std::move(raw_instructions)};
    EXPECT_THAT(vector_program.size(), Eq(TEST_SINGLE_ENTRY_COUNT));
    EXPECT_THAT(vector_program.begin()->opcode(), Eq(cpu::Opcode::HALT));
}

TEST(ExecutionTraceTest, ProvidesStandardContainerForTraceEntries)
{
    cpu::ExecutionTrace trace;
    EXPECT_TRUE(trace.empty());

    trace.reserve(TEST_PROGRAM_RESERVE_COUNT);
    trace.push_back(cpu::ExecutionTraceEntry{
        TEST_TRACE_FIRST_CYCLE,
        TEST_FIRST_RIP,
        TEST_SECOND_RIP,
        cpu::Opcode::MOV,
        TEST_TRACE_NOT_HALTED});

    EXPECT_FALSE(trace.empty());
    EXPECT_THAT(trace.size(), Eq(TEST_SINGLE_ENTRY_COUNT));
    EXPECT_THAT(trace.at(0).opcode, Eq(cpu::Opcode::MOV));
    EXPECT_THAT(trace.entries().front().rip_after, Eq(TEST_SECOND_RIP));
    EXPECT_THAT(trace.begin()->cycle, Eq(TEST_TRACE_FIRST_CYCLE));

    auto trace_iterator = trace.begin();
    ++trace_iterator;
    EXPECT_TRUE(trace_iterator == trace.end());

    trace.clear();
    EXPECT_TRUE(trace.empty());
}
