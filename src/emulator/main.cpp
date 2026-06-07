#include <cstddef>
#include <iostream>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/platform/machine.hpp>

namespace cpu = mnos::cpu;
namespace kernel = mnos::os::kernel;
namespace platform = mnos::os::platform;

namespace
{
constexpr std::size_t EMULATOR_BOOTSTRAP_MEMORY_SIZE_BYTES = 16384;
constexpr cpu::SignedQword EMULATOR_BOOTSTRAP_MEMORY_BASE = cpu::SignedQword{64};
constexpr cpu::SignedQword EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT = cpu::SignedQword{8};
constexpr cpu::Address64 EMULATOR_BOOTSTRAP_STORED_ADDRESS =
    static_cast<cpu::Address64>(EMULATOR_BOOTSTRAP_MEMORY_BASE + EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT);
}

int main()
{
    platform::Machine machine(EMULATOR_BOOTSTRAP_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    const cpu::ExecutableImage bootstrap_image{
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 1
        0x48, 0x81, 0xC0, 0x29, 0x00, 0x00, 0x00,                   // ADD RAX, 41
        0x48, 0x89, 0x45, 0x08,                                     // MOV [RBP + 8], RAX
        0x48, 0x8B, 0x5D, 0x08,                                     // MOV RBX, [RBP + 8]
        0xF4};                                                      // HLT

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, bootstrap_image, boot_context.memory_bus());

    std::cout << "MNOS emulator bootstrap: kernel=" << (os_kernel.is_booted() ? "booted" : "not-booted")
              << ", " << cpu::opcode_to_assembly_name(cpu::Opcode::HLT)
              << ", RAX=" << state.registers().read(cpu::RegisterId::RAX)
              << ", RBX=" << state.registers().read(cpu::RegisterId::RBX)
              << ", MEM[" << EMULATOR_BOOTSTRAP_STORED_ADDRESS
              << "]=" << machine.physical_memory().read_qword(EMULATOR_BOOTSTRAP_STORED_ADDRESS)
              << ", steps=" << executed_steps
              << ", cycles=" << executor.cycle_count() << '\n';
    return 0;
}
