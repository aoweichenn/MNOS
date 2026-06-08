#include <cstddef>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/host/terminal_backend.hpp>
#include <mnos/os/dev/terminal.hpp>

namespace dev = mnos::os::dev;
namespace host = mnos::host;

namespace
{
using ::testing::Eq;

constexpr std::string_view TEST_INVALID_ENUM_NAME = "<invalid>";
constexpr std::string_view TEST_ENTER_INPUT = "\n";
constexpr std::string_view TEST_BACKSPACE_INPUT = "\b";
constexpr std::string_view TEST_DELETE_INPUT = "\x7F";
constexpr std::string_view TEST_TAB_INPUT = "\t";
constexpr std::string_view TEST_NO_TERMINAL_INPUT = "";
}

TEST(HostTerminalBackendTest, RenderModeAndInputEventCatalogsAreStable)
{
    EXPECT_FALSE(host::terminal_render_mode_is_screen(host::TerminalRenderMode::ANSI_STREAM));
    EXPECT_FALSE(host::terminal_render_mode_is_screen(host::TerminalRenderMode::PLAIN_STREAM));
    EXPECT_TRUE(host::terminal_render_mode_is_screen(host::TerminalRenderMode::ANSI_SCREEN));
    EXPECT_TRUE(host::terminal_render_mode_is_screen(host::TerminalRenderMode::PLAIN_SCREEN));

    EXPECT_TRUE(host::terminal_render_mode_uses_ansi(host::TerminalRenderMode::ANSI_STREAM));
    EXPECT_FALSE(host::terminal_render_mode_uses_ansi(host::TerminalRenderMode::PLAIN_STREAM));
    EXPECT_TRUE(host::terminal_render_mode_uses_ansi(host::TerminalRenderMode::ANSI_SCREEN));
    EXPECT_FALSE(host::terminal_render_mode_uses_ansi(host::TerminalRenderMode::PLAIN_SCREEN));

    EXPECT_TRUE(host::is_host_input_event_kind_valid(host::HostInputEventKind::TEXT));
    EXPECT_TRUE(host::is_host_input_event_kind_valid(host::HostInputEventKind::SPECIAL_KEY));
    EXPECT_TRUE(host::is_host_input_event_kind_valid(host::HostInputEventKind::INPUT_CLOSED));
    EXPECT_TRUE(host::is_host_input_event_kind_valid(host::HostInputEventKind::HOST_IO_ERROR));
    EXPECT_FALSE(host::is_host_input_event_kind_valid(host::HostInputEventKind::COUNT));

    EXPECT_THAT(
        host::host_input_event_kind_to_index(host::HostInputEventKind::HOST_IO_ERROR),
        Eq(std::size_t{3}));
    EXPECT_THAT(
        host::host_input_event_kind_to_name(host::HostInputEventKind::INPUT_CLOSED),
        Eq(std::string_view{"INPUT_CLOSED"}));
    EXPECT_THAT(
        host::host_input_event_kind_to_name(host::HostInputEventKind::COUNT),
        Eq(TEST_INVALID_ENUM_NAME));
}

TEST(HostTerminalBackendTest, SpecialKeyCatalogSeparatesLineEditingFromFutureControlKeys)
{
    EXPECT_TRUE(host::is_host_special_key_valid(host::HostSpecialKey::ENTER));
    EXPECT_TRUE(host::is_host_special_key_valid(host::HostSpecialKey::CTRL_D));
    EXPECT_FALSE(host::is_host_special_key_valid(host::HostSpecialKey::COUNT));

    EXPECT_THAT(host::host_special_key_to_index(host::HostSpecialKey::ARROW_LEFT), Eq(std::size_t{7}));
    EXPECT_THAT(
        host::host_special_key_to_name(host::HostSpecialKey::CTRL_C),
        Eq(std::string_view{"CTRL_C"}));
    EXPECT_THAT(host::host_special_key_to_name(host::HostSpecialKey::COUNT), Eq(TEST_INVALID_ENUM_NAME));

    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::ENTER), Eq(TEST_ENTER_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::BACKSPACE), Eq(TEST_BACKSPACE_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::DELETE_KEY), Eq(TEST_DELETE_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::TAB), Eq(TEST_TAB_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::ESCAPE), Eq(TEST_NO_TERMINAL_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::ARROW_UP), Eq(TEST_NO_TERMINAL_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::CTRL_C), Eq(TEST_NO_TERMINAL_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::CTRL_D), Eq(TEST_NO_TERMINAL_INPUT));
    EXPECT_THAT(host::host_special_key_to_terminal_input(host::HostSpecialKey::COUNT), Eq(TEST_NO_TERMINAL_INPUT));
}

TEST(HostTerminalBackendTest, InputEventsConvertOnlyTerminalVisibleInput)
{
    const host::HostInputEvent text_event = host::HostInputEvent::text("echo hello\n");
    const host::HostInputEvent enter_event = host::HostInputEvent::special_key(host::HostSpecialKey::ENTER);
    const host::HostInputEvent arrow_event = host::HostInputEvent::special_key(host::HostSpecialKey::ARROW_RIGHT);
    const host::HostInputEvent closed_event = host::HostInputEvent::input_closed();
    const host::HostInputEvent error_event = host::HostInputEvent::host_io_error();

    EXPECT_THAT(text_event.kind(), Eq(host::HostInputEventKind::TEXT));
    EXPECT_TRUE(text_event.has_text());
    EXPECT_FALSE(text_event.has_special_key());
    EXPECT_THAT(text_event.text(), Eq(std::string_view{"echo hello\n"}));
    EXPECT_THAT(host::host_input_event_to_terminal_input(text_event), Eq(std::string{"echo hello\n"}));

    EXPECT_THAT(enter_event.kind(), Eq(host::HostInputEventKind::SPECIAL_KEY));
    EXPECT_FALSE(enter_event.has_text());
    EXPECT_TRUE(enter_event.has_special_key());
    EXPECT_THAT(enter_event.special_key(), Eq(host::HostSpecialKey::ENTER));
    EXPECT_THAT(host::host_input_event_to_terminal_input(enter_event), Eq(std::string{"\n"}));

    EXPECT_THAT(host::host_input_event_to_terminal_input(arrow_event), Eq(std::string{}));
    EXPECT_THAT(closed_event.kind(), Eq(host::HostInputEventKind::INPUT_CLOSED));
    EXPECT_THAT(host::host_input_event_to_terminal_input(closed_event), Eq(std::string{}));
    EXPECT_THAT(error_event.kind(), Eq(host::HostInputEventKind::HOST_IO_ERROR));
    EXPECT_THAT(host::host_input_event_to_terminal_input(error_event), Eq(std::string{}));
}

TEST(HostTerminalBackendTest, StreamBackendReadsLinesAsNormalizedTextEvents)
{
    std::istringstream input{"alpha\r\nbeta\n"};
    std::ostringstream output;
    host::StreamTerminalBackend backend{input, output, host::TerminalRenderMode::PLAIN_STREAM};

    const host::HostInputEvent first_event = backend.read_input_event();
    const host::HostInputEvent second_event = backend.read_input_event();
    const host::HostInputEvent final_event = backend.read_input_event();

    EXPECT_THAT(backend.render_mode(), Eq(host::TerminalRenderMode::PLAIN_STREAM));
    EXPECT_THAT(first_event.kind(), Eq(host::HostInputEventKind::TEXT));
    EXPECT_THAT(first_event.text(), Eq(std::string_view{"alpha\n"}));
    EXPECT_THAT(second_event.kind(), Eq(host::HostInputEventKind::TEXT));
    EXPECT_THAT(second_event.text(), Eq(std::string_view{"beta\n"}));
    EXPECT_THAT(final_event.kind(), Eq(host::HostInputEventKind::INPUT_CLOSED));
}

TEST(HostTerminalBackendTest, StreamBackendReportsInputIoFailureAsHostError)
{
    std::istringstream input;
    input.setstate(std::ios::badbit);
    std::ostringstream output;
    host::StreamTerminalBackend backend{input, output, host::TerminalRenderMode::PLAIN_STREAM};

    const host::HostInputEvent event = backend.read_input_event();

    EXPECT_THAT(event.kind(), Eq(host::HostInputEventKind::HOST_IO_ERROR));
}

TEST(HostTerminalBackendTest, StreamRendererDrainsIncrementalOutputWithoutReplay)
{
    std::istringstream input;
    std::ostringstream output;
    host::StreamTerminalBackend backend{input, output, host::TerminalRenderMode::PLAIN_STREAM};
    dev::TerminalDevice terminal{std::size_t{12}, std::size_t{3}};

    terminal.write_output("hello");
    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{"hello"}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{1}));
    EXPECT_THAT(terminal.output_stream_size(), Eq(std::size_t{0}));

    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{"hello"}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{1}));

    terminal.write_output(" next");
    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{"hello next"}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{2}));
}

TEST(HostTerminalBackendTest, AnsiStreamRendererTranslatesBackspaceDeleteAndClear)
{
    std::istringstream input;
    std::ostringstream output;
    host::StreamTerminalBackend backend{input, output, host::TerminalRenderMode::ANSI_STREAM};
    dev::TerminalDevice terminal{std::size_t{12}, std::size_t{3}};

    terminal.write_output("a\b");
    terminal.write_output(std::string_view{"\x7F", std::size_t{1}});
    terminal.clear_display();

    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{"a\b \b\b \b\x1B[2J\x1B[H"}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{1}));
    EXPECT_THAT(terminal.output_stream_size(), Eq(std::size_t{0}));
}

TEST(HostTerminalBackendTest, PlainStreamRendererDiscardsClearWhenItHasNoTextOutput)
{
    std::istringstream input;
    std::ostringstream output;
    host::StreamTerminalBackend backend{input, output, host::TerminalRenderMode::PLAIN_STREAM};
    dev::TerminalDevice terminal{std::size_t{12}, std::size_t{3}};

    terminal.clear_display();

    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{0}));
    EXPECT_THAT(terminal.output_stream_size(), Eq(std::size_t{0}));
}

TEST(HostTerminalBackendTest, PlainScreenRendererKeepsExplicitDisplaySnapshots)
{
    std::istringstream input;
    std::ostringstream output;
    host::StreamTerminalBackend backend{input, output, host::TerminalRenderMode::PLAIN_SCREEN};
    dev::TerminalDevice terminal{std::size_t{12}, std::size_t{3}};

    terminal.write_output("abc");
    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{"abc\n"}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{1}));

    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{"abc\n"}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{1}));

    terminal.write_output("\nnext");
    EXPECT_TRUE(backend.render_terminal(terminal));
    EXPECT_THAT(output.str(), Eq(std::string{"abc\nabc\nnext\n"}));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{2}));
}

TEST(HostTerminalBackendTest, RendererReportsHostOutputFailure)
{
    std::istringstream input;
    std::ostringstream output;
    output.setstate(std::ios::badbit);
    host::StreamTerminalBackend backend{input, output, host::TerminalRenderMode::PLAIN_STREAM};
    dev::TerminalDevice terminal{std::size_t{12}, std::size_t{3}};

    terminal.write_output("fail");

    EXPECT_FALSE(backend.render_terminal(terminal));
    EXPECT_THAT(backend.render_count(), Eq(std::size_t{0}));
}
