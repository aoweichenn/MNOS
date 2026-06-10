#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/host/terminal_runner.hpp>
#include <mnos/os/io/file_descriptor.hpp>

namespace host = mnos::host;
namespace io = mnos::os::io;

namespace
{
using ::testing::Eq;
using ::testing::HasSubstr;

constexpr char TEST_ANSI_ESCAPE_CHARACTER = '\x1B';
constexpr std::string_view TEST_TERMINAL_PROMPT_LS = "mnos> ls";
constexpr std::string_view TEST_TERMINAL_MEMORY_REPORT_PREFIX = "memory_pages total=";
constexpr std::string_view TEST_TERMINAL_PROCESS_HEADER = "pid ppid state exit threads states";
constexpr std::string_view TEST_TERMINAL_PROCESS_ROW = "1 0 RUNNING - 1 RUNNING";
constexpr std::size_t TEST_EXPECTED_TWO_LS_COMMANDS = std::size_t{2};
constexpr std::size_t TEST_EXPECTED_SINGLE_COMMAND_OUTPUT = std::size_t{1};
constexpr std::size_t TEST_SECOND_RENDER_CALL_INDEX = std::size_t{1};

class ScriptedTerminalBackend final : public host::HostTerminalBackend
{
public:
    explicit ScriptedTerminalBackend(
        std::vector<host::HostInputEvent> events,
        std::optional<std::size_t> failed_render_call_index = std::nullopt) :
        events_(std::move(events)),
        failed_render_call_index_(failed_render_call_index),
        renderer_(host::TerminalRenderMode::PLAIN_STREAM)
    {
    }

    [[nodiscard]] host::HostInputEvent read_input_event() override
    {
        if (this->next_event_index_ >= this->events_.size())
        {
            return host::HostInputEvent::input_closed();
        }
        host::HostInputEvent event = std::move(this->events_[this->next_event_index_]);
        ++this->next_event_index_;
        return event;
    }

    [[nodiscard]] bool render_terminal(mnos::os::dev::TerminalDevice& terminal) override
    {
        if (this->failed_render_call_index_.has_value() &&
            this->render_call_index_ == this->failed_render_call_index_.value())
        {
            ++this->render_call_index_;
            return false;
        }
        ++this->render_call_index_;
        return this->renderer_.render_if_changed(terminal, this->output_);
    }

    [[nodiscard]] std::size_t render_count() const noexcept override
    {
        return this->renderer_.render_count();
    }

    [[nodiscard]] std::string output() const
    {
        return this->output_.str();
    }

private:
    std::vector<host::HostInputEvent> events_;
    std::size_t next_event_index_ = std::size_t{0};
    std::size_t render_call_index_ = std::size_t{0};
    std::optional<std::size_t> failed_render_call_index_;
    std::ostringstream output_;
    host::HostTerminalRenderer renderer_;
};

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

[[nodiscard]] host::TerminalRunner make_plain_stream_runner()
{
    host::TerminalRunnerConfig config;
    config.render_mode = host::TerminalRenderMode::PLAIN_STREAM;
    return host::TerminalRunner{config};
}

[[nodiscard]] host::TerminalRunner make_plain_raw_runner()
{
    host::TerminalRunnerConfig config;
    config.render_mode = host::TerminalRenderMode::PLAIN_STREAM;
    config.input_mode = host::TerminalInputMode::RAW;
    return host::TerminalRunner{config};
}
}

TEST(HostTerminalRunnerTest, ExecutesHelpAndExitThroughSimulatedTerminal)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input{"help\nexit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_TRUE(result.completed());
    EXPECT_FALSE(result.has_shell_io_status());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_GT(result.poll_count(), std::size_t{0});
    EXPECT_GT(result.render_count(), std::size_t{0});
    EXPECT_THAT(output.str(), HasSubstr("mnos> help"));
    EXPECT_THAT(output.str(), HasSubstr("builtins: help clear echo ps mem cpu ticks ls cat touch write stat run exit"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
    EXPECT_THAT(output.str().find(TEST_ANSI_ESCAPE_CHARACTER), Eq(std::string::npos));
}

TEST(HostTerminalRunnerTest, BackendOverloadAcceptsTextAndSpecialKeyEvents)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    ScriptedTerminalBackend backend{
        std::vector<host::HostInputEvent>{
            host::HostInputEvent::special_key(host::HostSpecialKey::ARROW_UP),
            host::HostInputEvent::text("echo event model"),
            host::HostInputEvent::special_key(host::HostSpecialKey::ENTER),
            host::HostInputEvent::text("exit"),
            host::HostInputEvent::special_key(host::HostSpecialKey::ENTER)}};

    const host::TerminalRunResult result = runner.run(backend);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_GT(result.poll_count(), std::size_t{0});
    EXPECT_GT(result.render_count(), std::size_t{0});
    EXPECT_THAT(backend.output(), HasSubstr("mnos> echo event model"));
    EXPECT_THAT(backend.output(), HasSubstr("event model\n"));
    EXPECT_THAT(backend.output(), HasSubstr("mnos> exit"));
}

TEST(HostTerminalRunnerTest, RawInputModeExecutesCommandsCharacterByCharacter)
{
    const host::TerminalRunner runner = make_plain_raw_runner();
    std::istringstream input{"echo raw mode\rexit\r"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_TRUE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> echo raw mode"));
    EXPECT_THAT(output.str(), HasSubstr("raw mode\n"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
}

TEST(HostTerminalRunnerTest, RawInputModeTreatsCtrlDAsInputClosed)
{
    const host::TerminalRunner runner = make_plain_raw_runner();
    const std::string input_text = std::string{"echo before eof\r"} + '\x04';
    std::istringstream input{input_text};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::INPUT_CLOSED));
    EXPECT_FALSE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(output.str(), HasSubstr("before eof\n"));
}

TEST(HostTerminalRunnerTest, FileCommandsPersistThroughShellSession)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input{
        "touch /note\n"
        "write /note hello terminal\n"
        "cat /note\n"
        "stat /note\n"
        "exit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{5}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> touch /note"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> write /note hello terminal"));
    EXPECT_THAT(output.str(), HasSubstr("hello terminal\n"));
    EXPECT_THAT(output.str(), HasSubstr("path=/note kind=file"));
}

TEST(HostTerminalRunnerTest, RunCommandExecutesSeededDemoElf)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input{
        "run /bin/exit42\n"
        "exit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> run /bin/exit42"));
    EXPECT_THAT(output.str(), HasSubstr("run /bin/exit42 pid=2 status=EXITED wait=EXITED exit=42 steps=3 trace=3"));
}

TEST(HostTerminalRunnerTest, DefaultStreamDoesNotReplayScreenSnapshots)
{
    const host::TerminalRunner runner;
    std::istringstream input{
        "ls\n"
        "ls\n"
        "mem\n"
        "ps\n"
        "exit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);
    const std::string rendered_output = output.str();

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{5}));
    EXPECT_THAT(count_substring(rendered_output, TEST_TERMINAL_PROMPT_LS), Eq(TEST_EXPECTED_TWO_LS_COMMANDS));
    EXPECT_THAT(
        count_substring(rendered_output, TEST_TERMINAL_MEMORY_REPORT_PREFIX),
        Eq(TEST_EXPECTED_SINGLE_COMMAND_OUTPUT));
    EXPECT_THAT(count_substring(rendered_output, TEST_TERMINAL_PROCESS_HEADER), Eq(TEST_EXPECTED_SINGLE_COMMAND_OUTPUT));
    EXPECT_THAT(count_substring(rendered_output, TEST_TERMINAL_PROCESS_ROW), Eq(TEST_EXPECTED_SINGLE_COMMAND_OUTPUT));
    EXPECT_THAT(rendered_output.find(TEST_ANSI_ESCAPE_CHARACTER), Eq(std::string::npos));
}

TEST(HostTerminalRunnerTest, ReportsInputClosedWhenHostEndsWithoutExitCommand)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input{"echo before eof\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::INPUT_CLOSED));
    EXPECT_FALSE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(output.str(), HasSubstr("before eof"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> "));
}

TEST(HostTerminalRunnerTest, AnsiStreamModeEmitsClearControlForClearCommand)
{
    host::TerminalRunnerConfig config;
    config.render_mode = host::TerminalRenderMode::ANSI_STREAM;
    const host::TerminalRunner runner{config};
    std::istringstream input{"clear\nexit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> clear\n\x1B[2J\x1B[3J\x1B[Hmnos> exit"));
}

TEST(HostTerminalRunnerTest, StreamModeTranslatesBackspaceEchoForHostTerminal)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    const std::string input_text = std::string{"echo ax"} + '\b' + "z\nexit\n";
    std::istringstream input{input_text};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str(), HasSubstr("x\b \bz"));
    EXPECT_THAT(output.str(), HasSubstr("az\n"));
}

TEST(HostTerminalRunnerTest, AnsiScreenModeEmitsFullScreenControlSequences)
{
    host::TerminalRunnerConfig config;
    config.render_mode = host::TerminalRenderMode::ANSI_SCREEN;
    const host::TerminalRunner runner{config};
    std::istringstream input{"exit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(output.str(), HasSubstr("\x1B[2J\x1B[3J\x1B[H"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
}

TEST(HostTerminalRunnerTest, PlainScreenModeKeepsSnapshotRenderingExplicit)
{
    host::TerminalRunnerConfig config;
    config.render_mode = host::TerminalRenderMode::PLAIN_SCREEN;
    const host::TerminalRunner runner{config};
    std::istringstream input{"help\nclear\nexit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{3}));
    EXPECT_THAT(output.str().find(TEST_ANSI_ESCAPE_CHARACTER), Eq(std::string::npos));
    EXPECT_THAT(output.str(), HasSubstr("builtins: help clear echo ps mem cpu ticks ls cat touch write stat run exit"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
}

TEST(HostTerminalRunnerTest, PlainStreamKeepsClearCommandInTextLogWithoutScreenReplay)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input{"clear\nexit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str().find(TEST_ANSI_ESCAPE_CHARACTER), Eq(std::string::npos));
    EXPECT_THAT(count_substring(output.str(), "mnos> clear"), Eq(TEST_EXPECTED_SINGLE_COMMAND_OUTPUT));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
}

TEST(HostTerminalRunnerTest, CarriageReturnLineEndingsAreNormalized)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input{"echo carriage\r\nexit\r\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> echo carriage"));
    EXPECT_THAT(output.str(), HasSubstr("carriage\n"));
}

TEST(HostTerminalRunnerTest, ReportsHostIoErrorWhenOutputStreamFails)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input{"exit\n"};
    std::ostringstream output;
    output.setstate(std::ios::badbit);

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::HOST_IO_ERROR));
    EXPECT_FALSE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{0}));
}

TEST(HostTerminalRunnerTest, ReportsHostIoErrorWhenInputStreamFails)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    std::istringstream input;
    input.setstate(std::ios::badbit);
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::HOST_IO_ERROR));
    EXPECT_FALSE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{0}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> "));
}

TEST(HostTerminalRunnerTest, ReportsHostIoErrorWhenBackendRenderFailsAfterInput)
{
    const host::TerminalRunner runner = make_plain_stream_runner();
    ScriptedTerminalBackend backend{
        std::vector<host::HostInputEvent>{host::HostInputEvent::text("echo render failed\n")},
        TEST_SECOND_RENDER_CALL_INDEX};

    const host::TerminalRunResult result = runner.run(backend);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::HOST_IO_ERROR));
    EXPECT_FALSE(result.completed());
    EXPECT_FALSE(result.has_shell_io_status());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{1}));
    EXPECT_GT(result.poll_count(), std::size_t{0});
    EXPECT_THAT(result.render_count(), Eq(std::size_t{1}));
    EXPECT_THAT(backend.output(), HasSubstr("mnos> "));
    EXPECT_THAT(backend.output().find("render failed\n"), Eq(std::string::npos));
}

TEST(HostTerminalRunnerTest, ResultFactoriesExposeErrorMetadataAndRunnerConfig)
{
    host::TerminalRunnerConfig config;
    config.physical_memory_size_bytes = host::HOST_TERMINAL_DEFAULT_MEMORY_SIZE_BYTES;
    config.processor_count = host::HOST_TERMINAL_DEFAULT_PROCESSOR_COUNT;
    config.render_mode = host::TerminalRenderMode::PLAIN_STREAM;
    const host::TerminalRunner runner{config};

    EXPECT_THAT(runner.config().processor_count, Eq(host::HOST_TERMINAL_DEFAULT_PROCESSOR_COUNT));
    EXPECT_THAT(runner.config().render_mode, Eq(host::TerminalRenderMode::PLAIN_STREAM));
    EXPECT_THAT(runner.config().input_mode, Eq(host::TerminalInputMode::AUTO));

    const host::TerminalRunResult shell_error = host::TerminalRunResult::shell_io_error(
        io::IoStatus::BAD_DESCRIPTOR,
        std::size_t{1},
        std::size_t{2},
        std::size_t{3});
    EXPECT_THAT(shell_error.status(), Eq(host::TerminalRunStatus::SHELL_IO_ERROR));
    EXPECT_TRUE(shell_error.has_shell_io_status());
    EXPECT_THAT(shell_error.shell_io_status(), Eq(io::IoStatus::BAD_DESCRIPTOR));
    EXPECT_THAT(shell_error.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(shell_error.poll_count(), Eq(std::size_t{2}));
    EXPECT_THAT(shell_error.render_count(), Eq(std::size_t{3}));

    const host::TerminalRunResult host_error =
        host::TerminalRunResult::host_io_error(std::size_t{4}, std::size_t{5}, std::size_t{6});
    EXPECT_THAT(host_error.status(), Eq(host::TerminalRunStatus::HOST_IO_ERROR));
    EXPECT_FALSE(host_error.has_shell_io_status());
    EXPECT_THAT(host_error.command_count(), Eq(std::size_t{4}));
    EXPECT_THAT(host_error.poll_count(), Eq(std::size_t{5}));
    EXPECT_THAT(host_error.render_count(), Eq(std::size_t{6}));
}
