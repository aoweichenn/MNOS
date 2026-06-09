#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/host/machine_session.hpp>
#include <mnos/os/dev/terminal.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace
{
namespace host = mnos::host;
namespace io = mnos::os::io;
namespace mm = mnos::os::mm;

using ::testing::Eq;
using ::testing::HasSubstr;

constexpr std::string_view TEST_INVALID_ENUM_NAME = "<invalid>";
constexpr std::string_view TEST_PROMPT = "mnos> ";
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr std::uint32_t TEST_SINGLE_PROCESSOR_COUNT = std::uint32_t{1};
constexpr std::size_t TEST_UNBOOTABLE_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES / mm::AddressValue{2});
}

TEST(HostMachineSessionTest, StatusCatalogIsStable)
{
    EXPECT_TRUE(host::is_host_machine_session_status_valid(host::HostMachineSessionStatus::CREATED));
    EXPECT_TRUE(host::is_host_machine_session_status_valid(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_TRUE(host::is_host_machine_session_status_valid(host::HostMachineSessionStatus::EXITED));
    EXPECT_TRUE(host::is_host_machine_session_status_valid(host::HostMachineSessionStatus::SHELL_IO_ERROR));
    EXPECT_FALSE(host::is_host_machine_session_status_valid(host::HostMachineSessionStatus::COUNT));

    EXPECT_THAT(
        host::host_machine_session_status_to_index(host::HostMachineSessionStatus::EXITED),
        Eq(std::size_t{2}));
    EXPECT_THAT(
        host::host_machine_session_status_to_name(host::HostMachineSessionStatus::WAITING_FOR_INPUT),
        Eq(std::string_view{"WAITING_FOR_INPUT"}));
    EXPECT_THAT(
        host::host_machine_session_status_to_name(host::HostMachineSessionStatus::COUNT),
        Eq(TEST_INVALID_ENUM_NAME));
}

TEST(HostMachineSessionTest, SnapshotExposesCreatedStateBeforeBoot)
{
    host::HostMachineSessionConfig config;
    config.processor_count = TEST_PROCESSOR_COUNT;
    host::HostMachineSession session{config};

    const host::HostMachineSessionSnapshot snapshot = session.snapshot();

    EXPECT_FALSE(session.booted());
    EXPECT_THAT(session.status(), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(snapshot.status, Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(snapshot.poll_count, Eq(std::size_t{0}));
    EXPECT_THAT(snapshot.process_count, Eq(std::size_t{0}));
    EXPECT_THAT(snapshot.physical_memory_size_bytes, Eq(host::HOST_MACHINE_SESSION_DEFAULT_MEMORY_SIZE_BYTES));
    EXPECT_THAT(snapshot.processor_count, Eq(TEST_PROCESSOR_COUNT));
}

TEST(HostMachineSessionTest, AccessorsRejectUnbootedSession)
{
    host::HostMachineSession session;

    EXPECT_THROW(static_cast<void>(session.terminal_device()), std::logic_error);
    EXPECT_THROW(static_cast<void>(session.kernel()), std::logic_error);
    EXPECT_THROW(static_cast<void>(session.submit_input("echo nope\n")), std::logic_error);
}

TEST(HostMachineSessionTest, FailedBootLeavesSessionUnbooted)
{
    host::HostMachineSessionConfig config;
    config.physical_memory_size_bytes = TEST_UNBOOTABLE_MEMORY_SIZE_BYTES;
    host::HostMachineSession session{config};

    EXPECT_THROW(session.boot(), std::runtime_error);
    EXPECT_FALSE(session.booted());
    EXPECT_THAT(session.status(), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THROW(static_cast<void>(session.pump_until_waiting()), std::logic_error);
    EXPECT_THAT(session.snapshot().physical_memory_size_bytes, Eq(TEST_UNBOOTABLE_MEMORY_SIZE_BYTES));
}

TEST(HostMachineSessionTest, BootCreatesShellAndPromptForGuiLoop)
{
    host::HostMachineSession session;

    session.boot();
    const host::HostMachineSessionSnapshot snapshot = session.snapshot();
    const std::string_view output = session.terminal_device().output_stream_since(std::size_t{0});

    EXPECT_TRUE(session.booted());
    EXPECT_TRUE(session.waiting_for_input());
    EXPECT_FALSE(session.completed());
    EXPECT_THAT(session.status(), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.command_count(), Eq(std::size_t{0}));
    EXPECT_GT(session.poll_count(), std::size_t{0});
    EXPECT_THAT(output, HasSubstr(TEST_PROMPT));
    EXPECT_THAT(snapshot.process_count, Eq(std::size_t{1}));
    EXPECT_THAT(snapshot.terminal_output_stream_size, Eq(output.size()));
    EXPECT_THAT(snapshot.physical_page_count, Eq(host::HOST_MACHINE_SESSION_DEFAULT_MEMORY_PAGE_COUNT));
    EXPECT_GT(snapshot.free_page_count, std::size_t{0});
    EXPECT_GT(snapshot.allocated_page_count, std::size_t{0});
}

TEST(HostMachineSessionTest, PublicAccessorsExposeBootedRuntime)
{
    host::HostMachineSession session;
    session.boot();
    const host::HostMachineSession& const_session = session;

    EXPECT_THAT(session.machine().processor_count(), Eq(host::HOST_MACHINE_SESSION_DEFAULT_PROCESSOR_COUNT));
    EXPECT_THAT(const_session.machine().processor_count(), Eq(host::HOST_MACHINE_SESSION_DEFAULT_PROCESSOR_COUNT));
    EXPECT_THAT(session.kernel().process_count(), Eq(std::size_t{1}));
    EXPECT_THAT(const_session.kernel().process_count(), Eq(std::size_t{1}));
    EXPECT_EQ(&session.terminal_device(), &session.machine().terminal_device());
    EXPECT_EQ(&const_session.terminal_device(), &const_session.machine().terminal_device());
    EXPECT_THAT(session.shell_process().thread_count(), Eq(std::size_t{1}));
    EXPECT_THAT(const_session.shell_process().thread_count(), Eq(std::size_t{1}));
    EXPECT_EQ(&session.shell_thread(), &session.shell_process().thread_at(std::size_t{0}));
    EXPECT_EQ(&const_session.shell_thread(), &const_session.shell_process().thread_at(std::size_t{0}));
}

TEST(HostMachineSessionTest, SubmitInputExecutesCommandsAndUpdatesSnapshot)
{
    host::HostMachineSession session;
    session.boot();

    const host::HostMachineSessionStatus echo_status = session.submit_input("echo gui ready\n");
    const host::HostMachineSessionSnapshot snapshot = session.snapshot();
    const std::string output{session.terminal_device().output_stream_since(std::size_t{0})};

    EXPECT_THAT(echo_status, Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(session.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(output, HasSubstr("mnos> echo gui ready"));
    EXPECT_THAT(output, HasSubstr("gui ready\n"));
    EXPECT_THAT(output, HasSubstr(TEST_PROMPT));
}

TEST(HostMachineSessionTest, EmptyAndHostOnlyInputEventsDoNotMutateSession)
{
    host::HostMachineSession session;

    EXPECT_THAT(session.submit_input(""), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::host_io_error()),
        Eq(host::HostMachineSessionStatus::CREATED));

    session.boot();
    const std::size_t command_count_before_input = session.command_count();
    const std::size_t poll_count_before_input = session.poll_count();
    const std::size_t output_size_before_input = session.terminal_device().output_stream_size();

    EXPECT_THAT(session.submit_input(""), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::input_closed()),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::special_key(host::HostSpecialKey::CTRL_C)),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.command_count(), Eq(command_count_before_input));
    EXPECT_THAT(session.poll_count(), Eq(poll_count_before_input));
    EXPECT_THAT(session.terminal_device().output_stream_size(), Eq(output_size_before_input));
}

TEST(HostMachineSessionTest, SubmitInputEventSupportsCharacterByCharacterTerminalInput)
{
    host::HostMachineSession session;
    session.boot();

    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::text("echo event model")),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.command_count(), Eq(std::size_t{0}));
    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::special_key(host::HostSpecialKey::ARROW_UP)),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::special_key(host::HostSpecialKey::ENTER)),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));

    const std::string output{session.terminal_device().output_stream_since(std::size_t{0})};

    EXPECT_THAT(session.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(output, HasSubstr("mnos> echo event model"));
    EXPECT_THAT(output, HasSubstr("event model\n"));
}

TEST(HostMachineSessionTest, ClosedStdoutTransitionsSessionToShellIoError)
{
    host::HostMachineSession session;
    session.boot();

    ASSERT_TRUE(session.kernel().close_fd(session.shell_process(), io::FileDescriptor::stdout()));
    EXPECT_THAT(session.submit_input("echo stdout closed\n"), Eq(host::HostMachineSessionStatus::SHELL_IO_ERROR));

    const host::HostMachineSessionSnapshot snapshot = session.snapshot();
    EXPECT_FALSE(session.waiting_for_input());
    EXPECT_FALSE(session.completed());
    EXPECT_TRUE(session.has_shell_io_status());
    EXPECT_THAT(session.shell_io_status(), Eq(io::IoStatus::BAD_DESCRIPTOR));
    EXPECT_TRUE(snapshot.has_shell_io_status);
    EXPECT_THAT(snapshot.shell_io_status, Eq(io::IoStatus::BAD_DESCRIPTOR));
    EXPECT_THAT(snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(session.submit_input("echo ignored\n"), Eq(host::HostMachineSessionStatus::SHELL_IO_ERROR));
    EXPECT_THAT(session.pump_until_waiting(), Eq(host::HostMachineSessionStatus::SHELL_IO_ERROR));
}

TEST(HostMachineSessionTest, ExitCompletesAndIgnoresLaterInput)
{
    host::HostMachineSession session;
    session.boot();

    EXPECT_THAT(session.submit_input("exit\n"), Eq(host::HostMachineSessionStatus::EXITED));
    const std::size_t command_count_after_exit = session.command_count();
    const std::size_t output_size_after_exit = session.terminal_device().output_stream_size();

    EXPECT_TRUE(session.completed());
    EXPECT_FALSE(session.waiting_for_input());
    EXPECT_THAT(session.submit_input("echo ignored\n"), Eq(host::HostMachineSessionStatus::EXITED));
    EXPECT_THAT(session.command_count(), Eq(command_count_after_exit));
    EXPECT_THAT(session.terminal_device().output_stream_size(), Eq(output_size_after_exit));
}

TEST(HostMachineSessionTest, ResetRebootsFreshMachineState)
{
    host::HostMachineSession session;
    session.boot();
    static_cast<void>(session.submit_input("echo before reset\n"));
    static_cast<void>(session.submit_input("exit\n"));

    session.reset();
    const std::string output{session.terminal_device().output_stream_since(std::size_t{0})};

    EXPECT_TRUE(session.booted());
    EXPECT_TRUE(session.waiting_for_input());
    EXPECT_THAT(session.command_count(), Eq(std::size_t{0}));
    EXPECT_GT(session.poll_count(), std::size_t{0});
    EXPECT_THAT(output, HasSubstr(TEST_PROMPT));
    EXPECT_THAT(output.find("before reset"), Eq(std::string::npos));
    EXPECT_THAT(session.snapshot().process_count, Eq(std::size_t{1}));
}

TEST(HostMachineSessionTest, MoveTransfersBootedRuntimeForGuiOwnership)
{
    host::HostMachineSessionConfig config;
    config.processor_count = TEST_SINGLE_PROCESSOR_COUNT;
    host::HostMachineSession source{config};
    source.boot();
    static_cast<void>(source.submit_input("echo before move\n"));

    host::HostMachineSession moved{std::move(source)};
    EXPECT_TRUE(moved.booted());
    EXPECT_THAT(moved.config().processor_count, Eq(TEST_SINGLE_PROCESSOR_COUNT));
    EXPECT_THAT(moved.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(moved.submit_input("echo after move\n"), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));

    host::HostMachineSession assigned;
    assigned = std::move(moved);
    const std::string output{assigned.terminal_device().output_stream_since(std::size_t{0})};

    EXPECT_TRUE(assigned.booted());
    EXPECT_TRUE(assigned.waiting_for_input());
    EXPECT_THAT(assigned.config().processor_count, Eq(TEST_SINGLE_PROCESSOR_COUNT));
    EXPECT_THAT(assigned.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(assigned.snapshot().processor_count, Eq(TEST_SINGLE_PROCESSOR_COUNT));
    EXPECT_THAT(output, HasSubstr("before move\n"));
    EXPECT_THAT(output, HasSubstr("after move\n"));
}
