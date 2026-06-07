#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>
#include <support/test_assert.hpp>
#include <cpu/support/cpu_test_helpers.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;
namespace cpu_support = mnos::test::cpu_support;

namespace
{
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

void test_cpu_state()
{
    cpu::CpuState state;
    const cpu::CpuState& const_state = state;
    test::check(state.rip() == cpu::CPU_STATE_INITIAL_RIP, "cpu state initial RIP mismatch");
    test::check(!state.is_halted(), "cpu state should not start halted");
    state.registers().write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    state.flags().write(cpu::FlagId::ZF, true);
    state.advance_rip();
    state.halt();
    test::check(const_state.registers().read(cpu::RegisterId::RAX) == TEST_REGISTER_VALUE,
                "const registers view mismatch");
    test::check(const_state.flags().read(cpu::FlagId::ZF), "const flags view mismatch");
    test::check(state.rip() == cpu::CPU_STATE_NEXT_INSTRUCTION_OFFSET, "cpu state advance RIP mismatch");
    test::check(state.is_halted(), "cpu state halt mismatch");
    state.resume();
    test::check(!state.is_halted(), "cpu state resume mismatch");
    state.reset();
    test::check(state.rip() == cpu::CPU_STATE_INITIAL_RIP, "cpu state reset RIP mismatch");
    test::check(state.registers().read(cpu::RegisterId::RAX) == cpu::UQWORD64{0},
                "cpu state reset registers mismatch");
    test::check(!state.flags().read(cpu::FlagId::ZF), "cpu state reset flags mismatch");
}

void test_program_container()
{
    cpu::Program program;
    test::check(program.empty(), "program should start empty");
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu::Instruction::make_halt());
    test::check(!program.empty(), "program should not be empty after push");
    test::check(program.size() == TEST_TWO_INSTRUCTION_COUNT, "program size mismatch");
    test::check(program.contains_rip(TEST_FIRST_RIP), "program should contain RIP 0");
    test::check(!program.contains_rip(TEST_TWO_INSTRUCTION_END_RIP), "program should reject end RIP");
    test::check(program.at(0).opcode() == cpu::Opcode::MOV, "program at mismatch");
    test::check(program.instruction_at(TEST_SECOND_RIP).opcode() == cpu::Opcode::HALT,
                "program instruction_at mismatch");
    test::check(program.instructions().size() == program.size(), "program span size mismatch");
    test::check(program.begin()->opcode() == cpu::Opcode::MOV, "program begin mismatch");
    auto program_iterator = program.begin();
    ++program_iterator;
    ++program_iterator;
    test::check(program_iterator == program.end(), "program end mismatch");
    test::check_throws<std::out_of_range>(
        [&program]() {
            static_cast<void>(program.instruction_at(TEST_TWO_INSTRUCTION_END_RIP));
        },
        "program invalid RIP");
    program.clear();
    test::check(program.empty(), "program clear mismatch");

    const cpu::Program initialized_program{
        cpu::Instruction::make_halt(),
    };
    test::check(initialized_program.size() == TEST_SINGLE_ENTRY_COUNT, "initializer-list program size mismatch");

    std::vector<cpu::Instruction> raw_instructions;
    raw_instructions.push_back(cpu::Instruction::make_halt());
    const cpu::Program vector_program{std::move(raw_instructions)};
    test::check(vector_program.size() == TEST_SINGLE_ENTRY_COUNT, "vector program size mismatch");
    test::check(vector_program.begin()->opcode() == cpu::Opcode::HALT, "vector program opcode mismatch");
}

void test_execution_trace_container()
{
    cpu::ExecutionTrace trace;
    test::check(trace.empty(), "trace should start empty");
    trace.reserve(TEST_PROGRAM_RESERVE_COUNT);
    trace.push_back(cpu::ExecutionTraceEntry{
        TEST_TRACE_FIRST_CYCLE,
        TEST_FIRST_RIP,
        TEST_SECOND_RIP,
        cpu::Opcode::MOV,
        TEST_TRACE_NOT_HALTED});
    test::check(!trace.empty(), "trace should not be empty after push");
    test::check(trace.size() == TEST_SINGLE_ENTRY_COUNT, "trace size mismatch");
    test::check(trace.at(0).opcode == cpu::Opcode::MOV, "trace at mismatch");
    test::check(trace.entries().front().rip_after == TEST_SECOND_RIP, "trace span mismatch");
    test::check(trace.begin()->cycle == TEST_TRACE_FIRST_CYCLE, "trace begin mismatch");
    auto trace_iterator = trace.begin();
    ++trace_iterator;
    test::check(trace_iterator == trace.end(), "trace end mismatch");
    trace.clear();
    test::check(trace.empty(), "trace clear mismatch");
}
}

int main()
{
    test_cpu_state();
    test_program_container();
    test_execution_trace_container();

    std::cout << "mnos_cpu_execution_state_unit_tests passed\n";
    return 0;
}
