#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/dev/terminal.hpp>
#include <mnos/os/mm/address.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/tty/console.hpp>

namespace dev = mnos::os::dev;
namespace mm = mnos::os::mm;
namespace sched = mnos::os::sched;
namespace tty = mnos::os::tty;

namespace
{
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

constexpr std::size_t TEST_DISPLAY_COLUMNS = std::size_t{4};
constexpr std::size_t TEST_DISPLAY_ROWS = std::size_t{2};
constexpr std::size_t TEST_SMALL_COLUMNS = std::size_t{3};
constexpr std::size_t TEST_SMALL_ROWS = std::size_t{2};
constexpr std::size_t TEST_TINY_COLUMNS = std::size_t{2};
constexpr std::size_t TEST_TINY_ROWS = std::size_t{1};
constexpr std::size_t TEST_OVERFLOW_ROW_COUNT = std::size_t{2};
constexpr std::uint8_t TEST_TEXT_ATTRIBUTE = std::uint8_t{0x1E};
constexpr mm::VirtualAddress TEST_KERNEL_STACK_BOTTOM{0x7000'0000};
constexpr std::string_view TEST_CONSOLE_INPUT = "he\bllo\n";
constexpr std::string_view TEST_PARTIAL_INPUT = "abcdef\n";
constexpr std::string_view TEST_CLEARED_DRAFT_INPUT = "draft";
constexpr std::string_view TEST_WAKE_INPUT = "ok\n";
constexpr std::string_view TEST_TERMINAL_OUTPUT_TEXT = "xy";
constexpr std::size_t TEST_SMALL_READ_SIZE = std::size_t{3};

[[nodiscard]] std::string_view read_prefix(const std::array<char, 16>& buffer, const std::size_t size) noexcept
{
    return std::string_view{buffer.data(), size};
}
}

TEST(Stage12TerminalDeviceTest, TextDisplayWritesMovesErasesAndScrolls)
{
    EXPECT_THROW(static_cast<void>(dev::TextDisplayBuffer{std::size_t{0}, TEST_DISPLAY_ROWS}), std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(dev::TextDisplayBuffer{
            std::numeric_limits<std::size_t>::max(),
            TEST_OVERFLOW_ROW_COUNT}),
        std::overflow_error);

    dev::TextDisplayBuffer display{TEST_DISPLAY_COLUMNS, TEST_DISPLAY_ROWS};
    EXPECT_TRUE(display.empty());
    EXPECT_THAT(display.column_count(), Eq(TEST_DISPLAY_COLUMNS));
    EXPECT_THAT(display.row_count(), Eq(TEST_DISPLAY_ROWS));
    EXPECT_THAT(display.cell_at(std::size_t{0}, std::size_t{0}).attribute(), Eq(dev::TERMINAL_DEFAULT_ATTRIBUTE));

    display.write_string("ab\ncd");
    EXPECT_THAT(display.line(std::size_t{0}), Eq("ab  "));
    EXPECT_THAT(display.line(std::size_t{1}), Eq("cd  "));
    EXPECT_THAT(display.cursor_column(), Eq(std::size_t{2}));
    EXPECT_THAT(display.cursor_row(), Eq(std::size_t{1}));

    display.erase_previous_cell();
    EXPECT_THAT(display.line(std::size_t{1}), Eq("c   "));
    display.set_cursor(std::size_t{3}, std::size_t{1});
    EXPECT_THROW(static_cast<void>(display.cell_at(TEST_DISPLAY_COLUMNS, std::size_t{0})), std::out_of_range);
    EXPECT_THROW(display.set_cursor(TEST_DISPLAY_COLUMNS, std::size_t{0}), std::out_of_range);

    dev::TextDisplayBuffer small_display{TEST_SMALL_COLUMNS, TEST_SMALL_ROWS};
    small_display.write_string("a\nb\nc");
    EXPECT_THAT(small_display.scroll_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(small_display.line(std::size_t{0}), Eq("b  "));
    EXPECT_THAT(small_display.line(std::size_t{1}), Eq("c  "));
    EXPECT_THAT(small_display.render_text(), Eq("b  \nc  "));

    small_display.clear();
    EXPECT_TRUE(small_display.empty());
    EXPECT_THAT(small_display.cursor_column(), Eq(std::size_t{0}));
    EXPECT_THAT(small_display.cursor_row(), Eq(std::size_t{0}));
}

TEST(Stage12TerminalDeviceTest, TextDisplayCoversControlEdgesAndSnapshots)
{
    dev::TextCell colored_cell{'Z', TEST_TEXT_ATTRIBUTE};
    EXPECT_THAT(colored_cell.character(), Eq('Z'));
    EXPECT_THAT(colored_cell.attribute(), Eq(TEST_TEXT_ATTRIBUTE));

    dev::TextDisplayBuffer default_display;
    EXPECT_THAT(default_display.column_count(), Eq(dev::TERMINAL_DEFAULT_COLUMN_COUNT));
    EXPECT_THAT(default_display.row_count(), Eq(dev::TERMINAL_DEFAULT_ROW_COUNT));
    default_display.write_string("A\rB");
    EXPECT_THAT(default_display.line(std::size_t{0}).front(), Eq('B'));
    EXPECT_THROW(static_cast<void>(default_display.line(default_display.row_count())), std::out_of_range);
    EXPECT_THAT(default_display.lines(), SizeIs(dev::TERMINAL_DEFAULT_ROW_COUNT));

    dev::TextDisplayBuffer wrapped_display{TEST_TINY_COLUMNS, TEST_DISPLAY_ROWS};
    wrapped_display.write_string("ab");
    EXPECT_THAT(wrapped_display.cursor_column(), Eq(std::size_t{0}));
    EXPECT_THAT(wrapped_display.cursor_row(), Eq(std::size_t{1}));
    wrapped_display.write_char('c');
    EXPECT_THAT(wrapped_display.line(std::size_t{1}), Eq("c "));
    wrapped_display.erase_previous_cell();
    EXPECT_THAT(wrapped_display.line(std::size_t{1}), Eq("  "));
    wrapped_display.set_cursor(std::size_t{0}, std::size_t{1});
    wrapped_display.erase_previous_cell();
    EXPECT_THAT(wrapped_display.line(std::size_t{0}), Eq("a "));
    wrapped_display.set_cursor(std::size_t{0}, std::size_t{0});
    wrapped_display.erase_previous_cell();
    EXPECT_THAT(wrapped_display.line(std::size_t{0}), Eq("a "));

    dev::TextDisplayBuffer one_row_display{TEST_TINY_COLUMNS, TEST_TINY_ROWS};
    one_row_display.write_string("a\n");
    EXPECT_THAT(one_row_display.scroll_count(), Eq(std::uint64_t{1}));
    EXPECT_THAT(one_row_display.line(std::size_t{0}), Eq("  "));
}

TEST(Stage12TerminalDeviceTest, KeyboardQueueAndTerminalDevicePreserveInputOrderAndCapacity)
{
    dev::KeyEvent enter{dev::TERMINAL_NEWLINE_CHARACTER};
    dev::KeyEvent backspace{dev::TERMINAL_BACKSPACE_CHARACTER};
    dev::KeyEvent printable{'x'};
    dev::KeyEvent ignored_control{'\x01'};

    EXPECT_TRUE(enter.is_enter());
    EXPECT_TRUE(backspace.is_backspace());
    EXPECT_TRUE(printable.is_printable_ascii());
    EXPECT_FALSE(ignored_control.is_printable_ascii());

    dev::TerminalDevice terminal{TEST_DISPLAY_COLUMNS, TEST_DISPLAY_ROWS};
    terminal.keyboard().set_capacity(std::size_t{2});
    EXPECT_THAT(terminal.keyboard().capacity(), Eq(std::size_t{2}));
    EXPECT_THAT(terminal.submit_input("abc"), Eq(std::size_t{2}));
    EXPECT_FALSE(terminal.keyboard().has_capacity());
    EXPECT_THAT(terminal.keyboard().size(), Eq(std::size_t{2}));

    const std::vector<dev::KeyEvent> events = terminal.drain_input();
    ASSERT_THAT(events, SizeIs(2));
    EXPECT_THAT(events.at(std::size_t{0}).character(), Eq('a'));
    EXPECT_THAT(events.at(std::size_t{1}).character(), Eq('b'));
    EXPECT_TRUE(terminal.keyboard().empty());

    terminal.write_output(TEST_TERMINAL_OUTPUT_TEXT);
    EXPECT_THAT(terminal.display().line(std::size_t{0}), Eq("xy  "));
    EXPECT_THAT(terminal.output_stream_size(), Eq(TEST_TERMINAL_OUTPUT_TEXT.size()));
    EXPECT_THAT(terminal.output_stream_since(std::size_t{0}), Eq(TEST_TERMINAL_OUTPUT_TEXT));
    EXPECT_THAT(
        terminal.output_stream_since(TEST_TERMINAL_OUTPUT_TEXT.size() - std::size_t{1}),
        Eq(TEST_TERMINAL_OUTPUT_TEXT.substr(std::size_t{1})));

    terminal.clear_display();
    EXPECT_TRUE(terminal.display().empty());
    const std::string clear_output{dev::TERMINAL_CLEAR_SCREEN_CHARACTER};
    EXPECT_THAT(terminal.output_stream_since(TEST_TERMINAL_OUTPUT_TEXT.size()), Eq(clear_output));
    EXPECT_THROW(
        static_cast<void>(terminal.output_stream_since(terminal.output_stream_size() + std::size_t{1})),
        std::out_of_range);
    EXPECT_THROW(
        terminal.discard_output_stream_before(terminal.output_stream_size() + std::size_t{1}),
        std::out_of_range);
    terminal.discard_output_stream_before(TEST_TERMINAL_OUTPUT_TEXT.size());
    EXPECT_THAT(terminal.output_stream_since(std::size_t{0}), Eq(clear_output));
    terminal.discard_output_stream_before(terminal.output_stream_size());
    EXPECT_THAT(terminal.output_stream_since(std::size_t{0}), IsEmpty());

    dev::KeyboardInputQueue queue;
    EXPECT_TRUE(queue.push(dev::KeyEvent{'a'}));
    EXPECT_TRUE(queue.push(dev::KeyEvent{'b'}));
    EXPECT_TRUE(queue.push(dev::KeyEvent{'c'}));
    queue.set_capacity(std::size_t{2});
    EXPECT_THAT(queue.size(), Eq(std::size_t{2}));
    const std::vector<dev::KeyEvent> truncated_events = queue.drain();
    ASSERT_THAT(truncated_events, SizeIs(2));
    EXPECT_THAT(truncated_events.at(std::size_t{0}).character(), Eq('b'));
    EXPECT_THAT(truncated_events.at(std::size_t{1}).character(), Eq('c'));

    dev::TerminalDevice default_terminal;
    const dev::TerminalDevice& const_terminal = default_terminal;
    EXPECT_THAT(const_terminal.display().column_count(), Eq(dev::TERMINAL_DEFAULT_COLUMN_COUNT));
    EXPECT_TRUE(const_terminal.keyboard().empty());
}

TEST(Stage12ConsoleTest, ConsoleEchoesLineEditingAndBlocksUntilLineIsReady)
{
    dev::TerminalDevice terminal{std::size_t{10}, TEST_DISPLAY_ROWS};
    tty::Console console{terminal};
    sched::ThreadContext thread{sched::ThreadId{1}, TEST_KERNEL_STACK_BOTTOM};
    std::array<char, 16> buffer{};

    const tty::ConsoleReadResult blocked = console.read(buffer, thread);
    EXPECT_TRUE(blocked.is_blocked());
    EXPECT_THAT(thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_THAT(console.waiting_reader_count(), Eq(std::size_t{1}));

    const std::vector<sched::ThreadContext*> readers = console.submit_input(TEST_CONSOLE_INPUT);
    ASSERT_THAT(readers, SizeIs(1));
    EXPECT_EQ(readers.at(std::size_t{0}), &thread);
    EXPECT_THAT(console.waiting_reader_count(), Eq(std::size_t{0}));
    EXPECT_THAT(console.current_line_size(), Eq(std::size_t{0}));
    EXPECT_TRUE(console.has_ready_input());
    EXPECT_THAT(terminal.display().line(std::size_t{0}), Eq("hllo      "));

    const tty::ConsoleReadResult ready = console.read(buffer, thread);
    EXPECT_TRUE(ready.is_ready());
    EXPECT_THAT(ready.byte_count(), Eq(std::size_t{5}));
    EXPECT_THAT(read_prefix(buffer, ready.byte_count()), Eq(std::string_view{"hllo\n"}));
    EXPECT_FALSE(console.has_ready_input());
}

TEST(Stage12ConsoleTest, ConsoleSupportsPartialReadsAndEmptyReads)
{
    dev::TerminalDevice terminal{std::size_t{12}, TEST_DISPLAY_ROWS};
    tty::Console console{terminal};
    sched::ThreadContext thread{sched::ThreadId{2}, TEST_KERNEL_STACK_BOTTOM};
    std::array<char, 16> buffer{};

    EXPECT_TRUE(console.read(std::span<char>{}, thread).is_ready());
    EXPECT_THAT(console.waiting_reader_count(), Eq(std::size_t{0}));

    EXPECT_THAT(console.submit_input(TEST_PARTIAL_INPUT), IsEmpty());
    const tty::ConsoleReadResult first_read =
        console.read(std::span<char>{buffer.data(), TEST_SMALL_READ_SIZE}, thread);
    EXPECT_THAT(first_read.byte_count(), Eq(TEST_SMALL_READ_SIZE));
    EXPECT_THAT(read_prefix(buffer, first_read.byte_count()), Eq(std::string_view{"abc"}));
    EXPECT_THAT(console.ready_input_size(), Eq(std::size_t{4}));

    const tty::ConsoleReadResult second_read = console.read(buffer, thread);
    EXPECT_THAT(second_read.byte_count(), Eq(std::size_t{4}));
    EXPECT_THAT(read_prefix(buffer, second_read.byte_count()), Eq(std::string_view{"def\n"}));

    console.write("status");
    EXPECT_THAT(terminal.display().line(std::size_t{1}), Eq("status      "));
    console.clear();
    EXPECT_TRUE(terminal.display().empty());
    EXPECT_FALSE(console.has_ready_input());
}

TEST(Stage12ConsoleTest, ConsoleClearPreservesBlockedReadersUntilLineInputArrives)
{
    dev::TerminalDevice terminal{std::size_t{12}, TEST_DISPLAY_ROWS};
    tty::Console console{terminal};
    sched::ThreadContext thread{sched::ThreadId{3}, TEST_KERNEL_STACK_BOTTOM};
    std::array<char, 16> buffer{};

    const tty::ConsoleReadResult blocked = console.read(buffer, thread);
    EXPECT_TRUE(blocked.is_blocked());
    EXPECT_THAT(thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_THAT(console.waiting_reader_count(), Eq(std::size_t{1}));

    EXPECT_THAT(console.submit_input(TEST_CLEARED_DRAFT_INPUT), IsEmpty());
    EXPECT_THAT(console.current_line_size(), Eq(TEST_CLEARED_DRAFT_INPUT.size()));

    console.clear();
    EXPECT_THAT(thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_THAT(console.waiting_reader_count(), Eq(std::size_t{1}));
    EXPECT_THAT(console.current_line_size(), Eq(std::size_t{0}));
    EXPECT_TRUE(terminal.display().empty());

    const std::vector<sched::ThreadContext*> readers = console.submit_input(TEST_WAKE_INPUT);
    ASSERT_THAT(readers, SizeIs(1));
    EXPECT_EQ(readers.at(std::size_t{0}), &thread);
    EXPECT_THAT(console.waiting_reader_count(), Eq(std::size_t{0}));

    const tty::ConsoleReadResult ready = console.read(buffer, thread);
    EXPECT_TRUE(ready.is_ready());
    EXPECT_THAT(read_prefix(buffer, ready.byte_count()), Eq(TEST_WAKE_INPUT));
}
