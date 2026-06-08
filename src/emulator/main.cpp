#include <cstddef>
#include <iostream>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/perf/performance_model.hpp>
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
    os_kernel.console_write("MNOS terminal ready\n");

    const cpu::ExecutableImage bootstrap_image{
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 1
        0x48, 0x81, 0xC0, 0x29, 0x00, 0x00, 0x00,                   // ADD RAX, 41
        0x48, 0x89, 0x45, 0x08,                                     // MOV [RBP + 8], RAX
        0x48, 0x8B, 0x5D, 0x08,                                     // MOV RBX, [RBP + 8]
        0xF4};                                                      // HLT

    cpu::CpuState state;
    cpu::Executor executor;
    executor.enable_stage8_performance_model();
    const std::size_t executed_steps = executor.run(state, bootstrap_image, boot_context.memory_bus());
    const cpu::perf::PerformanceCounters& performance_counters =
        executor.stage8_performance_model().counters();

    std::cout << "MNOS emulator bootstrap: kernel=" << (os_kernel.is_booted() ? "booted" : "not-booted")
              << ", stage7=" << (os_kernel.has_stage7_services() ? "ready" : "not-ready")
              << ", stage8=" << (executor.has_stage8_performance_model() ? "ready" : "not-ready")
              << ", stage9=" << (os_kernel.has_stage9_services() ? "ready" : "not-ready")
              << ", stage10=" << (os_kernel.has_stage10_services() ? "ready" : "not-ready")
              << ", stage11=" << (os_kernel.has_stage11_services() ? "ready" : "not-ready")
              << ", stage12=" << (os_kernel.has_stage12_services() ? "ready" : "not-ready")
              << ", cores=" << os_kernel.bootstrap_processor_count()
              << ", terminal_scrolls=" << machine.terminal_device().display().scroll_count()
              << ", " << cpu::opcode_to_assembly_name(cpu::Opcode::HLT)
              << ", RAX=" << state.registers().read(cpu::RegisterId::RAX)
              << ", RBX=" << state.registers().read(cpu::RegisterId::RBX)
              << ", MEM[" << EMULATOR_BOOTSTRAP_STORED_ADDRESS
              << "]=" << machine.physical_memory().read_qword(EMULATOR_BOOTSTRAP_STORED_ADDRESS)
              << ", steps=" << executed_steps
              << ", cycles=" << executor.cycle_count()
              << ", perf_cycles=" << performance_counters.cycles()
              << ", l1i_misses=" << performance_counters.l1i_misses()
              << ", l1d_misses=" << performance_counters.l1d_misses() << '\n';
    return 0;
}
