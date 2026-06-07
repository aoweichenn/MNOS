#include <cstddef>
#include <iostream>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>

namespace cpu = mnos::cpu;

namespace
{
constexpr std::size_t EMULATOR_BOOTSTRAP_PROGRAM_INSTRUCTION_COUNT = 3;
constexpr cpu::SQWORD64 EMULATOR_BOOTSTRAP_INITIAL_RAX = cpu::SQWORD64{1};
constexpr cpu::SQWORD64 EMULATOR_BOOTSTRAP_RAX_INCREMENT = cpu::SQWORD64{41};
}

int main()
{
    cpu::Program program;
    program.reserve(EMULATOR_BOOTSTRAP_PROGRAM_INSTRUCTION_COUNT);
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::imm(EMULATOR_BOOTSTRAP_INITIAL_RAX)));
    program.push_back(cpu::Instruction::make_add(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::imm(EMULATOR_BOOTSTRAP_RAX_INCREMENT)));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program);

    std::cout << "MNOS emulator bootstrap: " << cpu::opcode_to_assembly_name(cpu::Opcode::HALT)
              << ", RAX=" << state.registers().read(cpu::RegisterId::RAX)
              << ", steps=" << executed_steps
              << ", cycles=" << executor.cycle_count() << '\n';
    return 0;
}
