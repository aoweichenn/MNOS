#include <cstddef>
#include <iostream>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/platform/machine.hpp>

namespace cpu = mnos::cpu;
namespace kernel = mnos::os::kernel;
namespace platform = mnos::os::platform;

namespace
{
constexpr std::size_t EMULATOR_BOOTSTRAP_PROGRAM_INSTRUCTION_COUNT = 6;
constexpr std::size_t EMULATOR_BOOTSTRAP_MEMORY_SIZE_BYTES = 16384;
constexpr cpu::SQWORD64 EMULATOR_BOOTSTRAP_INITIAL_RAX = cpu::SQWORD64{1};
constexpr cpu::SQWORD64 EMULATOR_BOOTSTRAP_RAX_INCREMENT = cpu::SQWORD64{41};
constexpr cpu::SQWORD64 EMULATOR_BOOTSTRAP_MEMORY_BASE = cpu::SQWORD64{64};
constexpr cpu::SQWORD64 EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT = cpu::SQWORD64{8};
constexpr cpu::ADDRESS64 EMULATOR_BOOTSTRAP_STORED_ADDRESS =
    static_cast<cpu::ADDRESS64>(EMULATOR_BOOTSTRAP_MEMORY_BASE + EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT);
}

int main()
{
    platform::Machine machine(EMULATOR_BOOTSTRAP_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    cpu::Program program;
    program.reserve(EMULATOR_BOOTSTRAP_PROGRAM_INSTRUCTION_COUNT);
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::imm(EMULATOR_BOOTSTRAP_INITIAL_RAX)));
    program.push_back(cpu::Instruction::make_add(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::imm(EMULATOR_BOOTSTRAP_RAX_INCREMENT)));
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RBP),
        cpu::Operand::imm(EMULATOR_BOOTSTRAP_MEMORY_BASE)));
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::mem(
            cpu::RegisterId::RBP,
            EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT,
            cpu::DataSize::QWORD),
        cpu::Operand::reg(cpu::RegisterId::RAX)));
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RBX),
        cpu::Operand::mem(
            cpu::RegisterId::RBP,
            EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT,
            cpu::DataSize::QWORD)));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, boot_context.memory_bus());

    std::cout << "MNOS emulator bootstrap: kernel=" << (os_kernel.is_booted() ? "booted" : "not-booted")
              << ", " << cpu::opcode_to_assembly_name(cpu::Opcode::HALT)
              << ", RAX=" << state.registers().read(cpu::RegisterId::RAX)
              << ", RBX=" << state.registers().read(cpu::RegisterId::RBX)
              << ", MEM[" << EMULATOR_BOOTSTRAP_STORED_ADDRESS
              << "]=" << machine.physical_memory().read_qword(EMULATOR_BOOTSTRAP_STORED_ADDRESS)
              << ", steps=" << executed_steps
              << ", cycles=" << executor.cycle_count() << '\n';
    return 0;
}
