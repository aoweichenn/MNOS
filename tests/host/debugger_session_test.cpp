#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/host/debugger_session.hpp>
#include <mnos/os/dev/terminal.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>

namespace
{
namespace host = mnos::host;
namespace io = mnos::os::io;
namespace mm = mnos::os::mm;

using ::testing::Eq;
using ::testing::HasSubstr;

constexpr std::string_view TEST_DEBUGGER_TITLE = "MNOS Test Debugger";
constexpr std::string_view TEST_NOT_BOOTED_TEXT = "not booted";
constexpr std::string_view TEST_PROMPT = "mnos> ";
constexpr std::size_t TEST_UNBOOTABLE_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES / mm::AddressValue{2});

[[nodiscard]] std::size_t count_substring(const std::string_view text, const std::string_view needle) noexcept
{
    std::size_t count = std::size_t{0};
    std::size_t offset = std::size_t{0};
    while (offset < text.size())
    {
        const std::size_t match_offset = text.find(needle, offset);
        if (match_offset == std::string_view::npos)
        {
            break;
        }
        ++count;
        offset = match_offset + needle.size();
    }
    return count;
}

[[nodiscard]] host::HostDebuggerSession make_titled_debugger_session()
{
    host::HostDebuggerSessionConfig config;
    config.title = std::string{TEST_DEBUGGER_TITLE};
    return host::HostDebuggerSession{config};
}
}

TEST(HostDebuggerSessionTest, FrameBeforeBootDescribesCreatedMachine)
{
    host::HostDebuggerSession session = make_titled_debugger_session();

    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_FALSE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(frame.display_text, HasSubstr(TEST_NOT_BOOTED_TEXT));
    EXPECT_THAT(frame.status_text, HasSubstr("state=CREATED"));
    EXPECT_THAT(frame.status_text, HasSubstr("booted=no"));
    EXPECT_THAT(frame.summary_text, HasSubstr(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(frame.processor_text, HasSubstr("processors=2"));
}

TEST(HostDebuggerSessionTest, PreBootInputOperationsAreSafeNoops)
{
    host::HostDebuggerSession session = make_titled_debugger_session();
    const host::HostDebuggerSession& const_session = session;

    EXPECT_THAT(const_session.config().title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_FALSE(const_session.machine_session().booted());
    EXPECT_FALSE(session.machine_session().booted());
    EXPECT_THAT(session.pump_until_waiting(), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(session.submit_text("echo ignored"), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(session.submit_command_line("echo ignored"), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(
        session.submit_special_key(host::HostSpecialKey::ENTER),
        Eq(host::HostMachineSessionStatus::CREATED));

    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_FALSE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.status_text, HasSubstr("state=CREATED"));
}

TEST(HostDebuggerSessionTest, BootProducesPromptAndMachineSummary)
{
    host::HostDebuggerSession session = make_titled_debugger_session();

    session.boot();
    session.boot();
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(frame.display_column_count, Eq(mnos::os::dev::TERMINAL_DEFAULT_COLUMN_COUNT));
    EXPECT_THAT(frame.display_row_count, Eq(mnos::os::dev::TERMINAL_DEFAULT_ROW_COUNT));
    EXPECT_THAT(frame.display_text, HasSubstr(TEST_PROMPT));
    EXPECT_THAT(frame.status_text, HasSubstr("accepts_input=yes"));
    EXPECT_THAT(frame.counters_text, HasSubstr("commands=0"));
    EXPECT_THAT(frame.memory_text, HasSubstr("memory_pages total=512"));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.cursor_text));
}

TEST(HostDebuggerSessionTest, MoveOperationsPreserveDebuggerState)
{
    host::HostDebuggerSession source = make_titled_debugger_session();
    source.boot();
    static_cast<void>(source.submit_command_line("echo movable"));

    host::HostDebuggerSession moved{std::move(source)};
    const host::HostDebuggerFrame moved_frame = moved.frame();

    EXPECT_TRUE(moved_frame.booted);
    EXPECT_THAT(moved_frame.title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(moved_frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(moved_frame.display_text, HasSubstr("movable"));

    host::HostDebuggerSession assigned;
    assigned = std::move(moved);
    const host::HostDebuggerFrame assigned_frame = assigned.frame();

    EXPECT_TRUE(assigned_frame.booted);
    EXPECT_THAT(assigned_frame.title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(assigned_frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(assigned_frame.display_text, HasSubstr("movable"));
}

TEST(HostDebuggerSessionTest, SubmitCommandLineNormalizesNewlineAndUpdatesFrame)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(
        session.submit_command_line("echo debugger ready"),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(frame.display_text, HasSubstr("mnos> echo debugger ready"));
    EXPECT_THAT(frame.display_text, HasSubstr("debugger ready"));
    EXPECT_THAT(frame.counters_text, HasSubstr("commands=1"));
}

TEST(HostDebuggerSessionTest, SubmitCommandLineAcceptsExistingCarriageReturn)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(
        session.submit_command_line("echo carriage\r"),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(frame.display_text, HasSubstr("carriage"));
}

TEST(HostDebuggerSessionTest, EmptyCommandLineStillSubmitsTerminalEnter)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(session.submit_command_line(""), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(count_substring(frame.display_text, TEST_PROMPT), Eq(std::size_t{2}));
}

TEST(HostDebuggerSessionTest, SpecialKeyInputSeparatesVisibleAndControlKeys)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(session.submit_text("echo key input"), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.submit_special_key(host::HostSpecialKey::ARROW_UP), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.frame().snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(session.submit_special_key(host::HostSpecialKey::ENTER), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(frame.display_text, HasSubstr("key input"));
}

TEST(HostDebuggerSessionTest, ResetRebootsFreshMachineFrame)
{
    host::HostDebuggerSession session;

    session.boot();
    static_cast<void>(session.submit_command_line("echo before reset"));
    session.reset();
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.display_text, HasSubstr(TEST_PROMPT));
    EXPECT_THAT(frame.display_text.find("before reset"), Eq(std::string::npos));
}

TEST(HostDebuggerSessionTest, ExitCommandMakesFrameReadOnlyUntilReset)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(session.submit_command_line("exit"), Eq(host::HostMachineSessionStatus::EXITED));
    EXPECT_THAT(session.pump_until_waiting(), Eq(host::HostMachineSessionStatus::EXITED));
    EXPECT_THAT(
        session.submit_command_line("echo ignored"),
        Eq(host::HostMachineSessionStatus::EXITED));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::EXITED));
    EXPECT_THAT(frame.status_text, HasSubstr("state=EXITED"));
}

TEST(HostDebuggerSessionTest, ShellIoErrorStatusAppearsInFrameSummary)
{
    host::HostDebuggerSession session;

    session.boot();
    ASSERT_TRUE(session.machine_session().kernel().close_fd(
        session.machine_session().shell_process(),
        io::FileDescriptor::stdout()));
    EXPECT_THAT(
        session.submit_command_line("echo closed"),
        Eq(host::HostMachineSessionStatus::SHELL_IO_ERROR));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_TRUE(frame.snapshot.has_shell_io_status);
    EXPECT_THAT(frame.snapshot.shell_io_status, Eq(io::IoStatus::BAD_DESCRIPTOR));
    EXPECT_THAT(frame.status_text, HasSubstr("state=SHELL_IO_ERROR"));
    EXPECT_THAT(frame.status_text, HasSubstr("shell_io_status="));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.status_text));
}

TEST(HostDebuggerSessionTest, FailedBootLeavesCreatedFrame)
{
    host::HostDebuggerSessionConfig config;
    config.machine.physical_memory_size_bytes = TEST_UNBOOTABLE_MEMORY_SIZE_BYTES;
    host::HostDebuggerSession session{config};

    EXPECT_THROW(session.boot(), std::runtime_error);
    EXPECT_FALSE(session.machine_session().booted());
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_FALSE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(frame.snapshot.physical_memory_size_bytes, Eq(TEST_UNBOOTABLE_MEMORY_SIZE_BYTES));
}
