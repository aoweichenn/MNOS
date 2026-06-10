#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/os/fs/vfs.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/shell/session.hpp>
#include <support/elf64_test_image.hpp>

namespace cpu = mnos::cpu;
namespace fs = mnos::os::fs;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;
namespace shell = mnos::os::shell;
namespace elf_test = mnos::tests::support;

namespace
{
using ::testing::Eq;
using ::testing::HasSubstr;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{512});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr std::size_t TEST_EXEC_MAX_STEPS = std::size_t{16};
constexpr std::int64_t TEST_EXIT_CODE = std::int64_t{42};
constexpr std::string_view TEST_BIN_DIRECTORY = "/bin";
constexpr std::string_view TEST_EXIT42_PATH = "/bin/exit42";
constexpr std::string_view TEST_BAD_ELF_PATH = "/bin/badelf";
constexpr std::string_view TEST_BAD_RANGE_ELF_PATH = "/bin/badrange";

[[nodiscard]] std::string terminal_output_text(const platform::Machine& machine)
{
    const std::string_view output = machine.terminal_device().output_stream_since(std::size_t{0});
    return std::string{output};
}

void install_executable(
    kernel::Kernel& os_kernel,
    const std::string_view path,
    const std::span<const cpu::Byte> bytes)
{
    if (!os_kernel.vfs().lookup(TEST_BIN_DIRECTORY).has_value())
    {
        static_cast<void>(os_kernel.vfs().create_directory(TEST_BIN_DIRECTORY));
    }
    static_cast<void>(os_kernel.vfs().create_file(path));
    fs::VfsFile file = os_kernel.vfs().open_file(path, fs::VfsOpenMode::READ_WRITE);
    EXPECT_THAT(file.write(bytes), Eq(bytes.size()));
}
}

TEST(Stage17ExecFileIntegrationTest, KernelExecUserFileLoadsElf64FromVfsAndReapsChild)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();
    sched::ThreadContext& parent_thread = os_kernel.create_thread(parent);
    const std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64(TEST_EXIT_CODE);
    install_executable(os_kernel, TEST_EXIT42_PATH, std::span<const cpu::Byte>{bytes});

    const kernel::UserProcessRunResult run_result =
        os_kernel.exec_user_file(parent.id(), TEST_EXIT42_PATH, TEST_EXEC_MAX_STEPS);

    EXPECT_THAT(run_result.status(), Eq(kernel::UserProcessRunStatus::EXITED));
    EXPECT_TRUE(run_result.has_exit_code());
    EXPECT_THAT(run_result.exit_code(), Eq(TEST_EXIT_CODE));
    EXPECT_THAT(run_result.executed_step_count(), Eq(std::size_t{3}));
    ASSERT_THAT(run_result.trace().size(), Eq(std::size_t{3}));
    EXPECT_THAT(run_result.trace().at(std::size_t{0}).opcode, Eq(cpu::Opcode::MOV));
    EXPECT_THAT(run_result.trace().at(std::size_t{2}).opcode, Eq(cpu::Opcode::SYSCALL));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    EXPECT_THAT(os_kernel.process_at(std::size_t{1}).parent_id(), Eq(parent.id()));
    EXPECT_TRUE(os_kernel.has_last_user_execution_trace());

    const kernel::ProcessWaitResult wait_result =
        os_kernel.wait_process(parent, parent_thread, run_result.process_id());
    EXPECT_THAT(wait_result.status(), Eq(kernel::ProcessWaitStatus::EXITED));
    EXPECT_THAT(wait_result.exit_code(), Eq(TEST_EXIT_CODE));
    EXPECT_TRUE(os_kernel.process_at(std::size_t{1}).is_reaped());
}

TEST(Stage17ExecFileIntegrationTest, ShellRunExecutesVfsElf64ThroughSessionContext)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);
    const std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64(TEST_EXIT_CODE);
    install_executable(os_kernel, TEST_EXIT42_PATH, std::span<const cpu::Byte>{bytes});
    shell::ShellSession session{os_kernel, shell_process, shell_thread};

    static_cast<void>(os_kernel.submit_terminal_input("run /bin/exit42\n"));
    const shell::ShellSessionStepResult step = session.poll();

    EXPECT_THAT(step.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(step.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_TRUE(session.prompt_pending());
    const std::string output = terminal_output_text(machine);
    EXPECT_THAT(output, HasSubstr("run /bin/exit42 pid=2 status=EXITED wait=EXITED exit=42 steps=3 trace=3"));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    EXPECT_TRUE(os_kernel.process_at(std::size_t{1}).is_reaped());
}

TEST(Stage17ExecFileIntegrationTest, ShellRunReportsPathAndArgumentErrors)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);
    shell::ShellSession session{os_kernel, shell_process, shell_thread};

    static_cast<void>(os_kernel.submit_terminal_input(
        "run\n"
        "run relative\n"
        "run /\n"
        "run /missing\n"
        "run /missing 0\n"));

    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));

    const std::string output = terminal_output_text(machine);
    EXPECT_THAT(output, HasSubstr("usage: run path [max_steps]"));
    EXPECT_THAT(output, HasSubstr("invalid path: relative"));
    EXPECT_THAT(output, HasSubstr("is a directory: /"));
    EXPECT_THAT(output, HasSubstr("not found: /missing"));
    EXPECT_THAT(output, HasSubstr("run error: invalid max_steps"));
}

TEST(Stage17ExecFileIntegrationTest, ShellRunSupportsMaxStepLimitWithoutWaiting)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);
    const std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64(TEST_EXIT_CODE);
    install_executable(os_kernel, TEST_EXIT42_PATH, std::span<const cpu::Byte>{bytes});
    shell::ShellSession session{os_kernel, shell_process, shell_thread};

    static_cast<void>(os_kernel.submit_terminal_input("run /bin/exit42 1\n"));
    const shell::ShellSessionStepResult step = session.poll();

    EXPECT_THAT(step.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(step.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    const std::string output = terminal_output_text(machine);
    EXPECT_THAT(output, HasSubstr("run /bin/exit42 pid=2 status=MAX_STEPS steps=1 trace=1"));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    EXPECT_TRUE(os_kernel.process_at(std::size_t{1}).is_running());
}

TEST(Stage17ExecFileIntegrationTest, ShellRunReportsInvalidExecutableErrors)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);

    const std::vector<cpu::Byte> bad_bytes{cpu::Byte{'n'}, cpu::Byte{'o'}, cpu::Byte{'p'}, cpu::Byte{'e'}};
    install_executable(os_kernel, TEST_BAD_ELF_PATH, std::span<const cpu::Byte>{bad_bytes});
    std::vector<cpu::Byte> bad_range_bytes = elf_test::make_test_exit_elf64(TEST_EXIT_CODE);
    elf_test::test_elf64_write_u64_le(
        bad_range_bytes,
        elf_test::TEST_ELF64_PH_VADDR_OFFSET,
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value());
    install_executable(os_kernel, TEST_BAD_RANGE_ELF_PATH, std::span<const cpu::Byte>{bad_range_bytes});
    shell::ShellSession session{os_kernel, shell_process, shell_thread};

    static_cast<void>(os_kernel.submit_terminal_input(
        "run /bin/badelf\n"
        "run /bin/badrange\n"));

    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    const std::string output = terminal_output_text(machine);
    EXPECT_THAT(output, HasSubstr("run error: invalid executable: elf64 file is smaller than the ELF header"));
    EXPECT_THAT(output, HasSubstr("run error: invalid executable: elf64 load segment must live inside user address range"));
    EXPECT_THAT(os_kernel.process_count(), Eq(std::size_t{1}));
}
