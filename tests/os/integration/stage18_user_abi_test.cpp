#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/os/fs/vfs.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/user_loader.hpp>
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

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{512});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr std::size_t TEST_EXEC_MAX_STEPS = std::size_t{16};
constexpr std::int64_t TEST_DEFAULT_ARGC = std::int64_t{1};
constexpr std::int64_t TEST_EXPLICIT_ARGC = std::int64_t{3};
constexpr std::string_view TEST_BIN_DIRECTORY = "/bin";
constexpr std::string_view TEST_ARGC_PATH = "/bin/argc";
constexpr cpu::Byte TEST_X86_REX_W = cpu::Byte{0x48};
constexpr cpu::Byte TEST_X86_MOV_RAX_IMM64 = cpu::Byte{0xB8};
constexpr cpu::Byte TEST_X86_MOV_R64_RM64 = cpu::Byte{0x8B};
constexpr cpu::Byte TEST_X86_MODRM_RDI_RSP_MEMORY = cpu::Byte{0x3C};
constexpr cpu::Byte TEST_X86_SIB_RSP_NO_INDEX = cpu::Byte{0x24};
constexpr cpu::Byte TEST_X86_SYSCALL_ESCAPE = cpu::Byte{0x0F};
constexpr cpu::Byte TEST_X86_SYSCALL = cpu::Byte{0x05};
constexpr cpu::Byte TEST_X86_HLT = cpu::Byte{0xF4};

void append_u64_le(std::vector<cpu::Byte>& bytes, const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes.push_back(static_cast<cpu::Byte>((value >> static_cast<unsigned>(byte_index * cpu::DATA_SIZE_BYTE_BITS)) &
                                               std::uint64_t{0xFF}));
    }
}

[[nodiscard]] std::vector<cpu::Byte> make_argc_exit_program_code()
{
    std::vector<cpu::Byte> code;
    code.reserve(std::size_t{16});
    code.push_back(TEST_X86_REX_W);
    code.push_back(TEST_X86_MOV_R64_RM64);
    code.push_back(TEST_X86_MODRM_RDI_RSP_MEMORY);
    code.push_back(TEST_X86_SIB_RSP_NO_INDEX);
    code.push_back(TEST_X86_REX_W);
    code.push_back(TEST_X86_MOV_RAX_IMM64);
    append_u64_le(code, static_cast<std::uint64_t>(kernel::SyscallNumber::EXIT));
    code.push_back(TEST_X86_SYSCALL_ESCAPE);
    code.push_back(TEST_X86_SYSCALL);
    code.push_back(TEST_X86_HLT);
    return code;
}

[[nodiscard]] std::vector<cpu::Byte> make_argc_exit_elf64()
{
    return elf_test::make_test_code_elf64(make_argc_exit_program_code());
}

[[nodiscard]] std::string terminal_output_text(const platform::Machine& machine)
{
    const std::string_view output = machine.terminal_device().output_stream_since(std::size_t{0});
    return std::string{output};
}

void install_executable(kernel::Kernel& os_kernel, const std::string_view path, const std::span<const cpu::Byte> bytes)
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

TEST(Stage18UserAbiIntegrationTest, KernelExecUserFileDefaultsArgv0ToPath)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();
    const std::vector<cpu::Byte> bytes = make_argc_exit_elf64();
    install_executable(os_kernel, TEST_ARGC_PATH, std::span<const cpu::Byte>{bytes});

    const kernel::UserProcessRunResult run_result =
        os_kernel.exec_user_file(parent.id(), TEST_ARGC_PATH, TEST_EXEC_MAX_STEPS);

    EXPECT_THAT(run_result.status(), Eq(kernel::UserProcessRunStatus::EXITED));
    EXPECT_TRUE(run_result.has_exit_code());
    EXPECT_THAT(run_result.exit_code(), Eq(TEST_DEFAULT_ARGC));
    EXPECT_THAT(run_result.executed_step_count(), Eq(std::size_t{3}));
    ASSERT_THAT(run_result.trace().size(), Eq(std::size_t{3}));
    EXPECT_THAT(run_result.trace().at(std::size_t{0}).opcode, Eq(cpu::Opcode::MOV));
    EXPECT_THAT(run_result.trace().at(std::size_t{2}).opcode, Eq(cpu::Opcode::SYSCALL));
}

TEST(Stage18UserAbiIntegrationTest, KernelExecUserFilePassesExplicitArguments)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();
    const std::vector<cpu::Byte> bytes = make_argc_exit_elf64();
    install_executable(os_kernel, TEST_ARGC_PATH, std::span<const cpu::Byte>{bytes});
    const proc::UserProgramArguments arguments{
        {std::string{TEST_ARGC_PATH}, std::string{"alpha"}, std::string{"beta"}}};

    const kernel::UserProcessRunResult run_result =
        os_kernel.exec_user_file(parent.id(), TEST_ARGC_PATH, arguments, TEST_EXEC_MAX_STEPS);

    EXPECT_THAT(run_result.status(), Eq(kernel::UserProcessRunStatus::EXITED));
    EXPECT_TRUE(run_result.has_exit_code());
    EXPECT_THAT(run_result.exit_code(), Eq(TEST_EXPLICIT_ARGC));
}

TEST(Stage18UserAbiIntegrationTest, ShellRunPassesArgumentsAndWaitsForArgcProgram)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);
    const std::vector<cpu::Byte> bytes = make_argc_exit_elf64();
    install_executable(os_kernel, TEST_ARGC_PATH, std::span<const cpu::Byte>{bytes});
    shell::ShellSession session{os_kernel, shell_process, shell_thread};

    static_cast<void>(os_kernel.submit_terminal_input("run /bin/argc alpha beta --max-steps=16\n"));
    const shell::ShellSessionStepResult step = session.poll();

    EXPECT_THAT(step.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(step.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    const std::string output = terminal_output_text(machine);
    EXPECT_THAT(output, HasSubstr("run /bin/argc pid=2 status=EXITED wait=EXITED "
                                  "exit=3 steps=3 trace=3"));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    EXPECT_TRUE(os_kernel.process_at(std::size_t{1}).is_reaped());
}

TEST(Stage18UserAbiIntegrationTest, ShellRunSupportsOptionTerminatorAndExplicitMaxSteps)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);
    const std::vector<cpu::Byte> bytes = make_argc_exit_elf64();
    install_executable(os_kernel, TEST_ARGC_PATH, std::span<const cpu::Byte>{bytes});
    shell::ShellSession session{os_kernel, shell_process, shell_thread};

    static_cast<void>(os_kernel.submit_terminal_input("run /bin/argc -- 7\n"
                                                      "run /bin/argc alpha --max-steps 1\n"
                                                      "run /bin/argc --max-steps-bad\n"));

    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(session.poll().status(), Eq(shell::ShellSessionStepStatus::COMMAND));

    const std::string output = terminal_output_text(machine);
    EXPECT_THAT(output, HasSubstr("run /bin/argc pid=2 status=EXITED wait=EXITED "
                                  "exit=2 steps=3 trace=3"));
    EXPECT_THAT(output, HasSubstr("run /bin/argc pid=3 status=MAX_STEPS steps=1 trace=1"));
    EXPECT_THAT(output, HasSubstr("run /bin/argc pid=4 status=EXITED wait=EXITED "
                                  "exit=2 steps=3 trace=3"));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{4}));
    EXPECT_TRUE(os_kernel.process_at(std::size_t{1}).is_reaped());
    EXPECT_TRUE(os_kernel.process_at(std::size_t{2}).is_running());
    EXPECT_TRUE(os_kernel.process_at(std::size_t{3}).is_reaped());
}
