#include <mnos/host/debugger_session.hpp>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/os/dev/terminal.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/sched/thread_state.hpp>

namespace cpu = mnos::cpu;
namespace dev = mnos::os::dev;
namespace sched = mnos::os::sched;

namespace
{
constexpr std::string_view HOST_DEBUGGER_ENUM_INVALID_NAME = "<invalid>";
constexpr std::string_view HOST_DEBUGGER_NOT_BOOTED_DISPLAY_TEXT =
    "MNOS machine is not booted.\nPress Reset or relaunch the debugger.";
constexpr std::string_view HOST_DEBUGGER_TRUE_TEXT = "yes";
constexpr std::string_view HOST_DEBUGGER_FALSE_TEXT = "no";
constexpr std::string_view HOST_DEBUGGER_LINE_SEPARATOR = "\n";
constexpr std::string_view HOST_DEBUGGER_NO_TRACE_TEXT = "trace empty";
constexpr std::string_view HOST_DEBUGGER_NOT_BOOTED_CPU_TEXT = "cpu unavailable: machine is not booted";
constexpr std::string_view HOST_DEBUGGER_RUN_ACTION = "run";
constexpr std::string_view HOST_DEBUGGER_STEP_ACTION = "step";
constexpr std::string_view HOST_DEBUGGER_PUMP_ACTION = "pump";
constexpr std::string_view HOST_DEBUGGER_BOOT_ACTION = "boot";
constexpr std::string_view HOST_DEBUGGER_RESET_ACTION = "reset";
constexpr std::string_view HOST_DEBUGGER_PAUSE_ACTION = "pause";
constexpr std::string_view HOST_DEBUGGER_TEXT_INPUT_ACTION = "input_text";
constexpr std::string_view HOST_DEBUGGER_COMMAND_INPUT_ACTION = "input_command";
constexpr std::string_view HOST_DEBUGGER_SPECIAL_INPUT_ACTION = "input_special";
constexpr std::string_view HOST_DEBUGGER_EVENT_INPUT_ACTION = "input_event";
constexpr std::string_view HOST_DEBUGGER_SKIPPED_DETAIL = "skipped";
constexpr std::size_t HOST_DEBUGGER_TRACE_PREVIEW_LIMIT = std::size_t{48};
constexpr int HOST_DEBUGGER_HEX_QWORD_WIDTH = 16;
constexpr std::size_t HOST_DEBUGGER_REGISTERS_PER_LINE = std::size_t{4};
constexpr char HOST_DEBUGGER_LINE_FEED_CHARACTER = '\n';
constexpr char HOST_DEBUGGER_CARRIAGE_RETURN_CHARACTER = '\r';
constexpr char HOST_DEBUGGER_TRACE_CONTROL_PLACEHOLDER = ' ';

class HostDebuggerRunStateCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::host::HostDebuggerRunState state) noexcept
    {
        return HOST_DEBUGGER_RUN_STATE_NAMES.contains(state);
    }

    [[nodiscard]] static std::size_t index(const mnos::host::HostDebuggerRunState state) noexcept
    {
        return HOST_DEBUGGER_RUN_STATE_NAMES.index(state);
    }

    [[nodiscard]] static std::string_view name(const mnos::host::HostDebuggerRunState state) noexcept
    {
        return HOST_DEBUGGER_RUN_STATE_NAMES.name(state);
    }

private:
    inline static constexpr auto HOST_DEBUGGER_RUN_STATE_NAMES =
        mnos::core::make_enum_name_table<mnos::host::HostDebuggerRunState>(
            std::array<std::string_view, mnos::host::HOST_DEBUGGER_RUN_STATE_COUNT>{
                "PAUSED",
                "RUNNING"},
            HOST_DEBUGGER_ENUM_INVALID_NAME);
};

inline constexpr std::array<cpu::RegisterId, cpu::REGISTER_ID_COUNT> HOST_DEBUGGER_REGISTER_ORDER{
    cpu::RegisterId::RAX,
    cpu::RegisterId::RBX,
    cpu::RegisterId::RCX,
    cpu::RegisterId::RDX,
    cpu::RegisterId::RSI,
    cpu::RegisterId::RDI,
    cpu::RegisterId::RBP,
    cpu::RegisterId::RSP,
    cpu::RegisterId::R8,
    cpu::RegisterId::R9,
    cpu::RegisterId::R10,
    cpu::RegisterId::R11,
    cpu::RegisterId::R12,
    cpu::RegisterId::R13,
    cpu::RegisterId::R14,
    cpu::RegisterId::R15};

[[nodiscard]] std::string bool_text(const bool value)
{
    return std::string{value ? HOST_DEBUGGER_TRUE_TEXT : HOST_DEBUGGER_FALSE_TEXT};
}

[[nodiscard]] std::string hex_qword(const cpu::Qword value)
{
    std::ostringstream output;
    output << "0x"
           << std::hex
           << std::uppercase
           << std::setfill('0')
           << std::setw(HOST_DEBUGGER_HEX_QWORD_WIDTH)
           << value;
    return output.str();
}

[[nodiscard]] std::string trace_preview(const std::string_view text)
{
    std::string result;
    result.reserve(std::min(text.size(), HOST_DEBUGGER_TRACE_PREVIEW_LIMIT));
    for (const char character : text)
    {
        if (result.size() >= HOST_DEBUGGER_TRACE_PREVIEW_LIMIT)
        {
            result.append("...");
            return result;
        }
        if (character == HOST_DEBUGGER_LINE_FEED_CHARACTER ||
            character == HOST_DEBUGGER_CARRIAGE_RETURN_CHARACTER)
        {
            result.push_back(HOST_DEBUGGER_TRACE_CONTROL_PLACEHOLDER);
        }
        else
        {
            result.push_back(character);
        }
    }
    return result;
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

[[nodiscard]] std::string make_text_input_detail(const std::string_view text)
{
    std::ostringstream output;
    output << "bytes=" << text.size();
    const std::string preview = trace_preview(text);
    if (!preview.empty())
    {
        output << " preview=\"" << preview << "\"";
    }
    return output.str();
}

[[nodiscard]] std::string make_special_key_detail(const mnos::host::HostSpecialKey key)
{
    std::ostringstream output;
    output << "key=" << mnos::host::host_special_key_to_name(key);
    return output.str();
}

[[nodiscard]] std::string make_input_event_detail(const mnos::host::HostInputEvent& event)
{
    std::ostringstream output;
    output << "kind=" << mnos::host::host_input_event_kind_to_name(event.kind());
    if (event.has_text())
    {
        output << " " << make_text_input_detail(event.text());
    }
    if (event.has_special_key())
    {
        output << " " << make_special_key_detail(event.special_key());
    }
    return output.str();
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

[[nodiscard]] std::string make_run_control_text(
    const mnos::host::HostDebuggerRunState run_state,
    const std::size_t trace_entry_count)
{
    std::ostringstream output;
    output << "debugger_run_state=" << mnos::host::host_debugger_run_state_to_name(run_state)
           << " trace_entries=" << trace_entry_count;
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

[[nodiscard]] std::string make_cpu_text(const sched::ThreadContext& thread)
{
    const cpu::CpuState& state = thread.cpu_state();
    const cpu::memory::PagingState& paging = state.paging();

    std::ostringstream output;
    output << "thread id=" << thread.id().value()
           << " state=" << sched::thread_state_to_name(thread.state())
           << " core=" << state.core_id().value()
           << " cpl=" << cpu::system::privilege_level_to_name(state.privilege_level())
           << " rip=" << hex_qword(state.rip())
           << " rsp=" << hex_qword(state.registers().read(cpu::RegisterId::RSP))
           << " rflags=" << hex_qword(state.flags().raw_bits())
           << " halted=" << bool_text(state.is_halted())
           << HOST_DEBUGGER_LINE_SEPARATOR;
    output << "paging enabled=" << bool_text(paging.is_enabled())
           << " cr3=" << hex_qword(paging.cr3())
           << " pcid_enabled=" << bool_text(paging.process_context_id_enabled())
           << " pcid=" << paging.process_context_id().value()
           << " cr2=" << hex_qword(paging.page_fault_linear_address())
           << " generation=" << paging.generation()
           << " wp=" << bool_text(paging.write_protect_enabled())
           << " nxe=" << bool_text(paging.execute_disable_enabled());

    for (std::size_t register_offset = std::size_t{0};
         register_offset < HOST_DEBUGGER_REGISTER_ORDER.size();
         ++register_offset)
    {
        if ((register_offset % HOST_DEBUGGER_REGISTERS_PER_LINE) == std::size_t{0})
        {
            output << HOST_DEBUGGER_LINE_SEPARATOR;
        }
        else
        {
            output << ' ';
        }

        const cpu::RegisterId register_id = HOST_DEBUGGER_REGISTER_ORDER[register_offset];
        output << cpu::register_id_to_assembly_name(register_id)
               << "="
               << hex_qword(state.registers().read(register_id));
    }

    return output.str();
}

[[nodiscard]] std::string make_trace_text(const std::deque<std::string>& trace_entries)
{
    if (trace_entries.empty())
    {
        return std::string{HOST_DEBUGGER_NO_TRACE_TEXT};
    }

    std::ostringstream output;
    for (std::size_t trace_index = std::size_t{0}; trace_index < trace_entries.size(); ++trace_index)
    {
        if (trace_index != std::size_t{0})
        {
            output << HOST_DEBUGGER_LINE_SEPARATOR;
        }
        output << trace_entries[trace_index];
    }
    return output.str();
}

[[nodiscard]] std::string make_summary_text(const mnos::host::HostDebuggerFrame& frame)
{
    std::ostringstream output;
    output << frame.title << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.run_control_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.status_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.counters_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.memory_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.processor_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.cursor_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.cpu_text << HOST_DEBUGGER_LINE_SEPARATOR
           << frame.trace_text;
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
bool is_host_debugger_run_state_valid(const HostDebuggerRunState state) noexcept
{
    return HostDebuggerRunStateCatalog::contains(state);
}

std::size_t host_debugger_run_state_to_index(const HostDebuggerRunState state) noexcept
{
    return HostDebuggerRunStateCatalog::index(state);
}

std::string_view host_debugger_run_state_to_name(const HostDebuggerRunState state) noexcept
{
    return HostDebuggerRunStateCatalog::name(state);
}

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

HostDebuggerRunState HostDebuggerSession::run_state() const noexcept
{
    return this->run_state_;
}

std::size_t HostDebuggerSession::trace_entry_count() const noexcept
{
    return this->trace_entries_.size();
}

void HostDebuggerSession::boot()
{
    if (this->machine_session_.booted())
    {
        this->append_trace(HOST_DEBUGGER_BOOT_ACTION, this->machine_session_.status(), HOST_DEBUGGER_SKIPPED_DETAIL);
        return;
    }

    try
    {
        this->machine_session_.boot();
        this->append_trace(HOST_DEBUGGER_BOOT_ACTION, this->machine_session_.status());
    }
    catch (...)
    {
        this->append_trace(HOST_DEBUGGER_BOOT_ACTION, this->machine_session_.status(), "failed");
        throw;
    }
}

void HostDebuggerSession::reset()
{
    this->run_state_ = HostDebuggerRunState::PAUSED;
    try
    {
        this->machine_session_.reset();
        this->append_trace(HOST_DEBUGGER_RESET_ACTION, this->machine_session_.status());
    }
    catch (...)
    {
        this->append_trace(HOST_DEBUGGER_RESET_ACTION, this->machine_session_.status(), "failed");
        throw;
    }
}

void HostDebuggerSession::pause()
{
    this->run_state_ = HostDebuggerRunState::PAUSED;
    this->append_trace(HOST_DEBUGGER_PAUSE_ACTION, this->machine_session_.status());
}

void HostDebuggerSession::clear_trace() noexcept
{
    this->trace_entries_.clear();
}

HostMachineSessionStatus HostDebuggerSession::pump_until_waiting()
{
    if (!this->machine_session_.booted())
    {
        this->append_trace(HOST_DEBUGGER_PUMP_ACTION, this->machine_session_.status(), HOST_DEBUGGER_SKIPPED_DETAIL);
        return this->machine_session_.status();
    }
    const HostMachineSessionStatus status = this->machine_session_.pump_until_waiting();
    this->append_trace(HOST_DEBUGGER_PUMP_ACTION, status);
    return status;
}

HostMachineSessionStatus HostDebuggerSession::step_until_waiting()
{
    if (!this->machine_session_.booted())
    {
        this->append_trace(HOST_DEBUGGER_STEP_ACTION, this->machine_session_.status(), HOST_DEBUGGER_SKIPPED_DETAIL);
        return this->machine_session_.status();
    }

    this->run_state_ = HostDebuggerRunState::RUNNING;
    const HostMachineSessionStatus status = this->machine_session_.pump_until_waiting();
    this->run_state_ = HostDebuggerRunState::PAUSED;
    this->append_trace(HOST_DEBUGGER_STEP_ACTION, status);
    return status;
}

HostMachineSessionStatus HostDebuggerSession::run_until_waiting()
{
    if (!this->machine_session_.booted())
    {
        this->append_trace(HOST_DEBUGGER_RUN_ACTION, this->machine_session_.status(), HOST_DEBUGGER_SKIPPED_DETAIL);
        return this->machine_session_.status();
    }

    this->run_state_ = HostDebuggerRunState::RUNNING;
    const HostMachineSessionStatus status = this->machine_session_.pump_until_waiting();
    this->run_state_ = HostDebuggerRunState::PAUSED;
    this->append_trace(HOST_DEBUGGER_RUN_ACTION, status);
    return status;
}

HostMachineSessionStatus HostDebuggerSession::submit_text(const std::string_view text)
{
    if (!this->machine_session_.booted())
    {
        this->append_trace(
            HOST_DEBUGGER_TEXT_INPUT_ACTION,
            this->machine_session_.status(),
            HOST_DEBUGGER_SKIPPED_DETAIL);
        return this->machine_session_.status();
    }
    const HostMachineSessionStatus status = this->machine_session_.submit_input(text);
    this->append_trace(HOST_DEBUGGER_TEXT_INPUT_ACTION, status, make_text_input_detail(text));
    return status;
}

HostMachineSessionStatus HostDebuggerSession::submit_command_line(const std::string_view command_line)
{
    if (!this->machine_session_.booted())
    {
        this->append_trace(
            HOST_DEBUGGER_COMMAND_INPUT_ACTION,
            this->machine_session_.status(),
            HOST_DEBUGGER_SKIPPED_DETAIL);
        return this->machine_session_.status();
    }
    const HostMachineSessionStatus status = this->machine_session_.submit_input(normalize_command_line(command_line));
    this->append_trace(HOST_DEBUGGER_COMMAND_INPUT_ACTION, status, make_text_input_detail(command_line));
    return status;
}

HostMachineSessionStatus HostDebuggerSession::submit_special_key(const HostSpecialKey key)
{
    if (!this->machine_session_.booted())
    {
        this->append_trace(
            HOST_DEBUGGER_SPECIAL_INPUT_ACTION,
            this->machine_session_.status(),
            HOST_DEBUGGER_SKIPPED_DETAIL);
        return this->machine_session_.status();
    }
    const HostMachineSessionStatus status = this->machine_session_.submit_input_event(HostInputEvent::special_key(key));
    this->append_trace(HOST_DEBUGGER_SPECIAL_INPUT_ACTION, status, make_special_key_detail(key));
    return status;
}

HostMachineSessionStatus HostDebuggerSession::submit_input_event(const HostInputEvent& event)
{
    if (!this->machine_session_.booted())
    {
        this->append_trace(
            HOST_DEBUGGER_EVENT_INPUT_ACTION,
            this->machine_session_.status(),
            HOST_DEBUGGER_SKIPPED_DETAIL);
        return this->machine_session_.status();
    }
    const HostMachineSessionStatus status = this->machine_session_.submit_input_event(event);
    this->append_trace(HOST_DEBUGGER_EVENT_INPUT_ACTION, status, make_input_event_detail(event));
    return status;
}

HostDebuggerFrame HostDebuggerSession::frame() const
{
    HostDebuggerFrame result;
    result.snapshot = this->machine_session_.snapshot();
    result.title = this->config_.title;
    result.run_state = this->run_state_;
    result.booted = this->machine_session_.booted();
    result.accepts_input = this->machine_session_.waiting_for_input();
    result.trace_entry_count = this->trace_entries_.size();

    if (result.booted)
    {
        fill_booted_display_frame(this->machine_session_.terminal_device(), result);
        result.cpu_text = make_cpu_text(this->machine_session_.shell_thread());
    }
    else
    {
        result.display_text = HOST_DEBUGGER_NOT_BOOTED_DISPLAY_TEXT;
        result.cpu_text = HOST_DEBUGGER_NOT_BOOTED_CPU_TEXT;
    }

    result.run_control_text = make_run_control_text(result.run_state, result.trace_entry_count);
    result.status_text = make_status_text(result.snapshot, result.booted, result.accepts_input);
    result.counters_text = make_counters_text(result.snapshot);
    result.memory_text = make_memory_text(result.snapshot);
    result.processor_text = make_processor_text(result.snapshot);
    result.cursor_text = make_cursor_text(result.cursor_column, result.cursor_row, result.scroll_count);
    result.trace_text = make_trace_text(this->trace_entries_);
    result.summary_text = make_summary_text(result);
    return result;
}

void HostDebuggerSession::append_trace(
    const std::string_view action,
    const HostMachineSessionStatus status,
    const std::string_view detail)
{
    const std::uint64_t sequence = this->next_trace_sequence_;
    ++this->next_trace_sequence_;

    if (this->config_.trace_capacity == std::size_t{0})
    {
        return;
    }

    while (this->trace_entries_.size() >= this->config_.trace_capacity)
    {
        this->trace_entries_.pop_front();
    }

    const HostMachineSessionSnapshot snapshot = this->machine_session_.snapshot();
    std::ostringstream output;
    output << "#" << sequence
           << " action=" << action
           << " status=" << host_machine_session_status_to_name(status)
           << " commands=" << snapshot.command_count
           << " polls=" << snapshot.poll_count;
    if (!detail.empty())
    {
        output << " " << detail;
    }
    this->trace_entries_.push_back(output.str());
}
}
