#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/shell/session.hpp>

namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;
namespace shell = mnos::os::shell;

namespace
{
using ::testing::Eq;
using ::testing::HasSubstr;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{256});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr std::string_view TEST_ECHO_READY_LINE = "echo ready\n";
constexpr std::string_view TEST_EXIT_LINE = "exit\n";

[[nodiscard]] std::string display_text(const platform::Machine& machine)
{
    return machine.terminal_device().display().render_text();
}

struct ShellSessionFixture final
{
    platform::Machine machine{TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT};
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
};
}

TEST(Stage14ShellSessionIntegrationTest, SessionBlocksThenTerminalInputWakesAndExecutesCommand)
{
    ShellSessionFixture fixture;
    fixture.os_kernel.boot();
    proc::Process& process = fixture.os_kernel.create_process();
    sched::ThreadContext& shell_thread = fixture.os_kernel.create_thread(process);
    sched::ThreadContext& worker_thread = fixture.os_kernel.create_thread(process);
    shell::ShellSession session{fixture.os_kernel, process, shell_thread};
    ASSERT_EQ(fixture.os_kernel.scheduler().schedule_next(), &shell_thread);

    const shell::ShellSessionStepResult blocked = session.poll();
    EXPECT_THAT(blocked.status(), Eq(shell::ShellSessionStepStatus::BLOCKED));
    EXPECT_TRUE(blocked.is_blocked());
    EXPECT_FALSE(session.prompt_pending());
    EXPECT_THAT(shell_thread.state(), Eq(sched::ThreadState::BLOCKED));
    ASSERT_TRUE(fixture.os_kernel.scheduler().has_current());
    EXPECT_EQ(&fixture.os_kernel.scheduler().current(), &worker_thread);
    EXPECT_THAT(display_text(fixture.machine), HasSubstr("mnos> "));

    const std::vector<sched::ThreadContext*> readers = fixture.os_kernel.submit_terminal_input(TEST_ECHO_READY_LINE);
    ASSERT_THAT(readers.size(), Eq(std::size_t{1}));
    EXPECT_EQ(readers.front(), &shell_thread);
    ASSERT_EQ(fixture.os_kernel.scheduler().schedule_next(), &shell_thread);

    const shell::ShellSessionStepResult command = session.poll();
    EXPECT_THAT(command.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(command.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_TRUE(session.running());
    EXPECT_TRUE(session.prompt_pending());
    EXPECT_FALSE(session.has_pending_input());
    EXPECT_THAT(display_text(fixture.machine), HasSubstr("ready"));

    const shell::ShellSessionStepResult blocked_again = session.poll();
    EXPECT_THAT(blocked_again.status(), Eq(shell::ShellSessionStepStatus::BLOCKED));
    EXPECT_FALSE(session.prompt_pending());

    static_cast<void>(fixture.os_kernel.submit_terminal_input(TEST_EXIT_LINE));
    ASSERT_EQ(fixture.os_kernel.scheduler().schedule_next(), &shell_thread);
    const shell::ShellSessionStepResult exited = session.poll();
    EXPECT_THAT(exited.status(), Eq(shell::ShellSessionStepStatus::EXITED));
    EXPECT_THAT(exited.command_status(), Eq(shell::ShellCommandStatus::EXIT_REQUESTED));
    EXPECT_FALSE(session.running());
    EXPECT_FALSE(session.prompt_pending());
}

TEST(Stage14ShellSessionIntegrationTest, SessionExecutesQueuedLinesOneAtATime)
{
    ShellSessionFixture fixture;
    fixture.os_kernel.boot();
    proc::Process& process = fixture.os_kernel.create_process();
    sched::ThreadContext& shell_thread = fixture.os_kernel.create_thread(process);
    shell::ShellSession session{fixture.os_kernel, process, shell_thread};

    static_cast<void>(fixture.os_kernel.submit_terminal_input("echo one\necho two\n"));

    const shell::ShellSessionStepResult first = session.poll();
    EXPECT_THAT(first.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(first.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_TRUE(session.has_pending_input());
    EXPECT_THAT(display_text(fixture.machine), HasSubstr("one"));

    const shell::ShellSessionStepResult second = session.poll();
    EXPECT_THAT(second.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(second.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_FALSE(session.has_pending_input());
    EXPECT_TRUE(session.prompt_pending());
    EXPECT_THAT(display_text(fixture.machine), HasSubstr("two"));
}

TEST(Stage14ShellSessionIntegrationTest, SessionPreservesLongPartialLinesAcrossPolls)
{
    ShellSessionFixture fixture;
    fixture.os_kernel.boot();
    proc::Process& process = fixture.os_kernel.create_process();
    sched::ThreadContext& shell_thread = fixture.os_kernel.create_thread(process);
    shell::ShellSession session{fixture.os_kernel, process, shell_thread};
    const std::string payload(shell::SHELL_SESSION_READ_BUFFER_SIZE + std::size_t{5}, 'x');
    const std::string input = std::string{"echo "} + payload + "\n";

    static_cast<void>(fixture.os_kernel.submit_terminal_input(input));

    const shell::ShellSessionStepResult pending = session.poll();
    EXPECT_THAT(pending.status(), Eq(shell::ShellSessionStepStatus::PENDING_INPUT));
    EXPECT_TRUE(session.has_pending_input());
    EXPECT_THAT(session.pending_input_size(), Eq(shell::SHELL_SESSION_READ_BUFFER_SIZE));
    EXPECT_FALSE(session.prompt_pending());

    const shell::ShellSessionStepResult command = session.poll();
    EXPECT_THAT(command.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_THAT(command.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_FALSE(session.has_pending_input());
    EXPECT_TRUE(session.prompt_pending());
    EXPECT_THAT(display_text(fixture.machine), HasSubstr(std::string(payload.substr(std::size_t{0}, std::size_t{16}))));
}
