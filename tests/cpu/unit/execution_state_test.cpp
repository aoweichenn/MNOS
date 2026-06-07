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
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/cpu/system/trap_frame.hpp>

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr cpu::Qword TEST_REGISTER_VALUE = cpu::Qword{0x1234ABCDULL};
constexpr cpu::SignedQword TEST_PROGRAM_INITIAL_VALUE = cpu::SignedQword{1};
constexpr std::size_t TEST_PROGRAM_RESERVE_COUNT = 8;
constexpr std::size_t TEST_SINGLE_ENTRY_COUNT = 1;
constexpr std::size_t TEST_TWO_INSTRUCTION_COUNT = 2;
constexpr cpu::InstructionPointer TEST_FIRST_RIP = cpu::InstructionPointer{0};
constexpr cpu::InstructionPointer TEST_SECOND_RIP = cpu::InstructionPointer{1};
constexpr cpu::InstructionPointer TEST_TWO_INSTRUCTION_END_RIP = cpu::InstructionPointer{2};
constexpr cpu::CycleCount TEST_TRACE_FIRST_CYCLE = cpu::CycleCount{1};
constexpr bool TEST_TRACE_NOT_HALTED = false;
constexpr cpu::InstructionPointer TEST_TRAP_RETURN_RIP = cpu::InstructionPointer{7};
constexpr cpu::Qword TEST_STACK_POINTER = cpu::Qword{0x8000};
constexpr cpu::Address64 TEST_PAGING_ROOT_TABLE = cpu::Address64{0x1000};
constexpr cpu::Address64 TEST_PAGE_FAULT_ADDRESS = cpu::Address64{0x5000};
}

TEST(CpuStateTest, TracksRegistersFlagsRipAndHaltState)
{
    cpu::CpuState state;
    const cpu::CpuState& const_state = state;

    EXPECT_THAT(state.rip(), Eq(cpu::CPU_STATE_INITIAL_RIP));
    EXPECT_FALSE(state.is_halted());

    state.registers().write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    state.flags().write(cpu::FlagId::ZF, true);
    state.paging().load_cr3(TEST_PAGING_ROOT_TABLE);
    state.paging().enable();
    state.paging().set_page_fault_linear_address(TEST_PAGE_FAULT_ADDRESS);
    state.advance_rip();
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.set_pending_trap(cpu_system::TrapFrame{
        cpu_system::TrapKind::SOFTWARE_INTERRUPT,
        cpu_system::InterruptVector::breakpoint(),
        state.rip(),
        TEST_TRAP_RETURN_RIP,
        state.flags().raw_bits(),
        TEST_STACK_POINTER,
        state.privilege_level()});
    state.halt();

    EXPECT_THAT(const_state.registers().read(cpu::RegisterId::RAX), Eq(TEST_REGISTER_VALUE));
    EXPECT_TRUE(const_state.flags().read(cpu::FlagId::ZF));
    EXPECT_TRUE(const_state.paging().is_enabled());
    EXPECT_THAT(const_state.paging().cr3(), Eq(TEST_PAGING_ROOT_TABLE));
    EXPECT_THAT(const_state.paging().page_fault_linear_address(), Eq(TEST_PAGE_FAULT_ADDRESS));
    EXPECT_THAT(state.rip(), Eq(cpu::CPU_STATE_NEXT_INSTRUCTION_OFFSET));
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(state.has_pending_trap());
    EXPECT_THAT(state.pending_trap().return_rip(), Eq(TEST_TRAP_RETURN_RIP));
    EXPECT_TRUE(state.is_halted());

    state.resume();
    EXPECT_FALSE(state.is_halted());
    state.clear_pending_trap();
    EXPECT_FALSE(state.has_pending_trap());

    state.reset();
    EXPECT_THAT(state.rip(), Eq(cpu::CPU_STATE_INITIAL_RIP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_FALSE(state.paging().is_enabled());
    EXPECT_THAT(state.paging().cr3(), Eq(cpu::Address64{0}));
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING0));
    EXPECT_THROW(static_cast<void>(state.pending_trap()), std::logic_error);
}

TEST(ProgramTest, ProvidesStandardContainerAndRipAccess)
{
    cpu::Program program;
    EXPECT_TRUE(program.empty());

    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    EXPECT_FALSE(program.empty());
    EXPECT_THAT(program.size(), Eq(TEST_TWO_INSTRUCTION_COUNT));
    EXPECT_TRUE(program.contains_rip(TEST_FIRST_RIP));
    EXPECT_FALSE(program.contains_rip(TEST_TWO_INSTRUCTION_END_RIP));
    EXPECT_THAT(program.at(0).opcode(), Eq(cpu::Opcode::MOV));
    EXPECT_THAT(program.instruction_at(TEST_SECOND_RIP).opcode(), Eq(cpu::Opcode::HLT));
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
        cpu::Instruction::make_hlt(),
    };
    EXPECT_THAT(initialized_program.size(), Eq(TEST_SINGLE_ENTRY_COUNT));

    std::vector<cpu::Instruction> raw_instructions;
    raw_instructions.push_back(cpu::Instruction::make_hlt());
    const cpu::Program vector_program{std::move(raw_instructions)};
    EXPECT_THAT(vector_program.size(), Eq(TEST_SINGLE_ENTRY_COUNT));
    EXPECT_THAT(vector_program.begin()->opcode(), Eq(cpu::Opcode::HLT));
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
    EXPECT_FALSE(trace.entries().front().trap_pending_after);
    EXPECT_THAT(trace.begin()->cycle_count, Eq(TEST_TRACE_FIRST_CYCLE));

    auto trace_iterator = trace.begin();
    ++trace_iterator;
    EXPECT_TRUE(trace_iterator == trace.end());

    trace.clear();
    EXPECT_TRUE(trace.empty());
}
