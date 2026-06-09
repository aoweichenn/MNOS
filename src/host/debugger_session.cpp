#include <mnos/host/debugger_session.hpp>

#include <sstream>
#include <string>
#include <utility>

#include <mnos/os/dev/terminal.hpp>

namespace dev = mnos::os::dev;

namespace
{
constexpr std::string_view HOST_DEBUGGER_NOT_BOOTED_DISPLAY_TEXT =
    "MNOS machine is not booted.\nPress Reset or relaunch the debugger.";
constexpr std::string_view HOST_DEBUGGER_TRUE_TEXT = "yes";
constexpr std::string_view HOST_DEBUGGER_FALSE_TEXT = "no";
constexpr std::string_view HOST_DEBUGGER_LINE_SEPARATOR = "\n";
constexpr char HOST_DEBUGGER_LINE_FEED_CHARACTER = '\n';
constexpr char HOST_DEBUGGER_CARRIAGE_RETURN_CHARACTER = '\r';

[[nodiscard]] std::string bool_text(const bool value)
{
    return std::string{value ? HOST_DEBUGGER_TRUE_TEXT : HOST_DEBUGGER_FALSE_TEXT};
}

[[nodiscard]] bool command_line_has_terminal_line_break(const std::string_view text) noexcept
{
    return !text.empty() &&
        (text.back() == HOST_DEBUGGER_LINE_FEED_CHARACTER ||
            text.back() == HOST_DEBUGGER_CARRIAGE_RETURN_CHARACTER);
}

[[nodiscard]] std::string normalize_command_line(const std::string_view command_line)
{
    std::string normalized{command_line};
    if (!command_line_has_terminal_line_break(normalized))
    {
        normalized.push_back(HOST_DEBUGGER_LINE_FEED_CHARACTER);
    }
    return normalized;
}

[[nodiscard]] std::string make_status_text(
    const mnos::host::HostMachineSessionSnapshot& snapshot,
    const bool booted,
    const bool accepts_input)
{
    std::ostringstream output;
    output << "state=" << mnos::host::host_machine_session_status_to_name(snapshot.status)
           << " booted=" << bool_text(booted)
           << " accepts_input=" << bool_text(accepts_input);
    if (snapshot.has_shell_io_status)
    {
        output << " shell_io_status=" << static_cast<unsigned>(snapshot.shell_io_status);
    }
    return output.str();
}

[[nodiscard]] std::string make_counters_text(const mnos::host::HostMachineSessionSnapshot& snapshot)
{
    std::ostringstream output;
    output << "commands=" << snapshot.command_count
           << " polls=" << snapshot.poll_count
           << " processes=" << snapshot.process_count
           << " terminal_bytes=" << snapshot.terminal_output_stream_size;
    return output.str();
}

[[nodiscard]] std::string make_memory_text(const mnos::host::HostMachineSessionSnapshot& snapshot)
{
    std::ostringstream output;
    output << "memory_pages total=" << snapshot.physical_page_count
           << " free=" << snapshot.free_page_count
           << " allocated=" << snapshot.allocated_page_count;
    return output.str();
}

[[nodiscard]] std::string make_processor_text(const mnos::host::HostMachineSessionSnapshot& snapshot)
{
    std::ostringstream output;
    output << "processors=" << snapshot.processor_count
           << " physical_bytes=" << snapshot.physical_memory_size_bytes;
    return output.str();
}

[[nodiscard]] std::string make_cursor_text(
    const std::size_t column,
    const std::size_t row,
    const std::uint64_t scroll_count)
{
    std::ostringstream output;
    output << "cursor row=" << row
           << " column=" << column
           << " scrolls=" << scroll_count;
    return output.str();
}

[[nodiscard]] std::string make_summary_text(const mnos::host::HostDebuggerFrame& frame)
{
    std::ostringstream output;
    output << frame.title << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.status_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.counters_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.memory_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.processor_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.cursor_text;
    return output.str();
}

void fill_booted_display_frame(
    const dev::TerminalDevice& terminal,
    mnos::host::HostDebuggerFrame& frame)
{
    const dev::TextDisplayBuffer& display = terminal.display();
    frame.display_column_count = display.column_count();
    frame.display_row_count = display.row_count();
    frame.cursor_column = display.cursor_column();
    frame.cursor_row = display.cursor_row();
    frame.scroll_count = display.scroll_count();
    frame.display_text = display.render_text();
}
}

namespace mnos::host
{
HostDebuggerSession::HostDebuggerSession(HostDebuggerSessionConfig config) noexcept :
    config_(std::move(config)),
    machine_session_(config_.machine)
{
}

HostDebuggerSession::HostDebuggerSession(HostDebuggerSession&&) noexcept = default;

HostDebuggerSession& HostDebuggerSession::operator=(HostDebuggerSession&&) noexcept = default;

HostDebuggerSession::~HostDebuggerSession() = default;

const HostDebuggerSessionConfig& HostDebuggerSession::config() const noexcept
{
    return this->config_;
}

const HostMachineSession& HostDebuggerSession::machine_session() const noexcept
{
    return this->machine_session_;
}

HostMachineSession& HostDebuggerSession::machine_session() noexcept
{
    return this->machine_session_;
}

void HostDebuggerSession::boot()
{
    if (this->machine_session_.booted())
    {
        return;
    }
    this->machine_session_.boot();
}

void HostDebuggerSession::reset()
{
    this->machine_session_.reset();
}

HostMachineSessionStatus HostDebuggerSession::pump_until_waiting()
{
    if (!this->machine_session_.booted())
    {
        return this->machine_session_.status();
    }
    return this->machine_session_.pump_until_waiting();
}

HostMachineSessionStatus HostDebuggerSession::submit_text(const std::string_view text)
{
    if (!this->machine_session_.booted())
    {
        return this->machine_session_.status();
    }
    return this->machine_session_.submit_input(text);
}

HostMachineSessionStatus HostDebuggerSession::submit_command_line(const std::string_view command_line)
{
    if (!this->machine_session_.booted())
    {
        return this->machine_session_.status();
    }
    return this->machine_session_.submit_input(normalize_command_line(command_line));
}

HostMachineSessionStatus HostDebuggerSession::submit_special_key(const HostSpecialKey key)
{
    if (!this->machine_session_.booted())
    {
        return this->machine_session_.status();
    }
    return this->machine_session_.submit_input_event(HostInputEvent::special_key(key));
}

HostDebuggerFrame HostDebuggerSession::frame() const
{
    HostDebuggerFrame result;
    result.snapshot = this->machine_session_.snapshot();
    result.title = this->config_.title;
    result.booted = this->machine_session_.booted();
    result.accepts_input = this->machine_session_.waiting_for_input();

    if (result.booted)
    {
        fill_booted_display_frame(this->machine_session_.terminal_device(), result);
    }
    else
    {
        result.display_text = HOST_DEBUGGER_NOT_BOOTED_DISPLAY_TEXT;
    }

    result.status_text = make_status_text(result.snapshot, result.booted, result.accepts_input);
    result.counters_text = make_counters_text(result.snapshot);
    result.memory_text = make_memory_text(result.snapshot);
    result.processor_text = make_processor_text(result.snapshot);
    result.cursor_text = make_cursor_text(result.cursor_column, result.cursor_row, result.scroll_count);
    result.summary_text = make_summary_text(result);
    return result;
}
}
