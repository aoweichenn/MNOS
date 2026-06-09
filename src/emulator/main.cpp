#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/perf/performance_model.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/kernel/syscall.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/user_loader.hpp>
#include <mnos/os/shell/session.hpp>

namespace cpu = mnos::cpu;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace shell = mnos::os::shell;

namespace
{
constexpr mm::AddressValue EMULATOR_BOOTSTRAP_MEMORY_PAGE_COUNT = mm::AddressValue{256};
constexpr std::size_t EMULATOR_BOOTSTRAP_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * EMULATOR_BOOTSTRAP_MEMORY_PAGE_COUNT);
constexpr cpu::SignedQword EMULATOR_BOOTSTRAP_MEMORY_BASE = cpu::SignedQword{64};
constexpr cpu::SignedQword EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT = cpu::SignedQword{8};
constexpr cpu::Address64 EMULATOR_BOOTSTRAP_STORED_ADDRESS =
    static_cast<cpu::Address64>(EMULATOR_BOOTSTRAP_MEMORY_BASE + EMULATOR_BOOTSTRAP_MEMORY_DISPLACEMENT);
constexpr std::string_view EMULATOR_STAGE15_FILE_TEXT = "file ready";
constexpr mm::VirtualAddress EMULATOR_USER_TEXT_BASE = mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mm::AddressValue EMULATOR_USER_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr std::size_t EMULATOR_USER_EXEC_MAX_STEPS = std::size_t{16};
constexpr std::int64_t EMULATOR_USER_EXIT_CODE = std::int64_t{42};
constexpr cpu::Byte EMULATOR_X86_REX_W = cpu::Byte{0x48};
constexpr cpu::Byte EMULATOR_X86_MOV_RAX_IMM64 = cpu::Byte{0xB8};
constexpr cpu::Byte EMULATOR_X86_MOV_RDI_IMM64 = cpu::Byte{0xBF};
constexpr cpu::Byte EMULATOR_X86_SYSCALL_ESCAPE = cpu::Byte{0x0F};
constexpr cpu::Byte EMULATOR_X86_SYSCALL = cpu::Byte{0x05};
constexpr cpu::Byte EMULATOR_X86_HLT = cpu::Byte{0xF4};

void append_u64_le(std::vector<cpu::Byte>& bytes, const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes.push_back(static_cast<cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * cpu::DATA_SIZE_BYTE_BITS)) & std::uint64_t{0xFF}));
    }
}

[[nodiscard]] std::vector<cpu::Byte> make_user_exit_program_bytes()
{
    std::vector<cpu::Byte> bytes;
    bytes.reserve(std::size_t{24});
    bytes.push_back(EMULATOR_X86_REX_W);
    bytes.push_back(EMULATOR_X86_MOV_RAX_IMM64);
    append_u64_le(bytes, static_cast<std::uint64_t>(kernel::SyscallNumber::EXIT));
    bytes.push_back(EMULATOR_X86_REX_W);
    bytes.push_back(EMULATOR_X86_MOV_RDI_IMM64);
    append_u64_le(bytes, static_cast<std::uint64_t>(EMULATOR_USER_EXIT_CODE));
    bytes.push_back(EMULATOR_X86_SYSCALL_ESCAPE);
    bytes.push_back(EMULATOR_X86_SYSCALL);
    bytes.push_back(EMULATOR_X86_HLT);
    return bytes;
}

[[nodiscard]] mnos::os::proc::UserProgram make_user_exit_program(std::vector<cpu::Byte> text)
{
    mnos::os::proc::UserProgram program{EMULATOR_USER_TEXT_BASE};
    program.set_initial_stack_size_bytes(EMULATOR_USER_STACK_SIZE_BYTES);
    program.add_segment(mnos::os::proc::UserSegment::text(EMULATOR_USER_TEXT_BASE, std::move(text)));
    return program;
}
}

int main()
{
    platform::Machine machine(EMULATOR_BOOTSTRAP_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    os_kernel.console_write("MNOS terminal ready\n");
    auto& shell_process = os_kernel.create_process();
    auto& shell_thread = os_kernel.create_thread(shell_process);
    shell::ShellSession os_shell{os_kernel, shell_process, shell_thread};
    static_cast<void>(os_kernel.scheduler().schedule_next());
    const shell::ShellSessionStepResult shell_initial_step = os_shell.poll();
    static_cast<void>(os_kernel.submit_terminal_input("echo shell ready\n"));
    static_cast<void>(os_kernel.scheduler().schedule_next());
    const shell::ShellSessionStepResult shell_command_step = os_shell.poll();
    static_cast<void>(os_kernel.submit_terminal_input("touch /hello\nwrite /hello file ready\ncat /hello\n"));
    const shell::ShellSessionStepResult shell_touch_step = os_shell.poll();
    const shell::ShellSessionStepResult shell_write_step = os_shell.poll();
    const shell::ShellSessionStepResult shell_cat_step = os_shell.poll();
    const bool shell_loop_ready =
        shell_initial_step.status() == shell::ShellSessionStepStatus::BLOCKED &&
        shell_command_step.status() == shell::ShellSessionStepStatus::COMMAND &&
        shell_command_step.command_status() == shell::ShellCommandStatus::HANDLED;
    const bool shell_file_ready =
        shell_touch_step.status() == shell::ShellSessionStepStatus::COMMAND &&
        shell_write_step.status() == shell::ShellSessionStepStatus::COMMAND &&
        shell_cat_step.status() == shell::ShellSessionStepStatus::COMMAND &&
        shell_cat_step.command_status() == shell::ShellCommandStatus::HANDLED &&
        machine.terminal_device().display().render_text().find(EMULATOR_STAGE15_FILE_TEXT) != std::string::npos;

    const std::vector<cpu::Byte> user_exit_bytes = make_user_exit_program_bytes();
    mnos::os::proc::UserProgram user_exit_program = make_user_exit_program(user_exit_bytes);
    const cpu::ExecutableImage user_exit_image{
        cpu::ExecutableImage::container_type{user_exit_bytes.begin(), user_exit_bytes.end()},
        static_cast<cpu::InstructionPointer>(EMULATOR_USER_TEXT_BASE.value())};
    const kernel::UserProcessRunResult user_exec_result = os_kernel.exec_user_program(
        shell_process.id(),
        user_exit_program,
        user_exit_image,
        EMULATOR_USER_EXEC_MAX_STEPS);
    const kernel::ProcessWaitResult user_wait_result =
        os_kernel.wait_process(shell_process, shell_thread, user_exec_result.process_id());
    const bool user_exec_ready =
        user_exec_result.status() == kernel::UserProcessRunStatus::EXITED &&
        user_exec_result.has_exit_code() &&
        user_exec_result.exit_code() == EMULATOR_USER_EXIT_CODE &&
        user_wait_result.status() == kernel::ProcessWaitStatus::EXITED;

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
              << ", stage13=" << (os_kernel.has_stage13_services() ? "ready" : "not-ready")
              << ", stage14=" << (shell_loop_ready ? "ready" : "not-ready")
              << ", stage15=" << (os_kernel.has_stage15_services() && shell_file_ready ? "ready" : "not-ready")
              << ", stage16=" << (user_exec_ready ? "ready" : "not-ready")
              << ", shell_initial=" << shell::shell_session_step_status_to_name(shell_initial_step.status())
              << ", shell_step=" << shell::shell_session_step_status_to_name(shell_command_step.status())
              << ", shell_running=" << (os_shell.running() ? "true" : "false")
              << ", user_exec_status=" << kernel::user_process_run_status_to_name(user_exec_result.status())
              << ", user_wait_status=" << kernel::process_wait_status_to_name(user_wait_result.status())
              << ", user_exit=" << user_exec_result.exit_code()
              << ", user_trace=" << user_exec_result.trace().size()
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
