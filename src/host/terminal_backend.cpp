#include <algorithm>
#include <array>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mnos/core/enum_map.hpp>
#include <mnos/host/terminal_backend.hpp>
#include <mnos/os/dev/terminal.hpp>

namespace dev = mnos::os::dev;

namespace
{
constexpr std::string_view HOST_TERMINAL_ENUM_INVALID_NAME = "<invalid>";
constexpr std::string_view HOST_TERMINAL_ANSI_CLEAR_SCREEN = "\x1B[2J";
constexpr std::string_view HOST_TERMINAL_ANSI_CURSOR_HOME = "\x1B[H";
constexpr std::string_view HOST_TERMINAL_BACKSPACE_ERASE = "\b \b";
constexpr std::string_view HOST_TERMINAL_ENTER_INPUT = "\n";
constexpr std::string_view HOST_TERMINAL_BACKSPACE_INPUT = "\b";
constexpr std::string_view HOST_TERMINAL_DELETE_INPUT = "\x7F";
constexpr std::string_view HOST_TERMINAL_TAB_INPUT = "\t";
constexpr std::string_view HOST_TERMINAL_NO_TERMINAL_INPUT = "";
constexpr char HOST_TERMINAL_NEWLINE_CHARACTER = '\n';
constexpr char HOST_TERMINAL_CARRIAGE_RETURN_CHARACTER = '\r';

class HostInputEventKindCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::host::HostInputEventKind kind) noexcept
    {
        return HOST_INPUT_EVENT_KIND_NAMES.contains(kind);
    }

    [[nodiscard]] static std::size_t index(const mnos::host::HostInputEventKind kind) noexcept
    {
        return HOST_INPUT_EVENT_KIND_NAMES.index(kind);
    }

    [[nodiscard]] static std::string_view name(const mnos::host::HostInputEventKind kind) noexcept
    {
        return HOST_INPUT_EVENT_KIND_NAMES.name(kind);
    }

private:
    inline static constexpr auto HOST_INPUT_EVENT_KIND_NAMES =
        mnos::core::make_enum_name_table<mnos::host::HostInputEventKind>(
            std::array<std::string_view, mnos::host::HOST_INPUT_EVENT_KIND_COUNT>{
                "TEXT",
                "SPECIAL_KEY",
                "INPUT_CLOSED",
                "HOST_IO_ERROR"},
            HOST_TERMINAL_ENUM_INVALID_NAME);
};

class HostSpecialKeyCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::host::HostSpecialKey key) noexcept
    {
        return HOST_SPECIAL_KEY_NAMES.contains(key);
    }

    [[nodiscard]] static std::size_t index(const mnos::host::HostSpecialKey key) noexcept
    {
        return HOST_SPECIAL_KEY_NAMES.index(key);
    }

    [[nodiscard]] static std::string_view name(const mnos::host::HostSpecialKey key) noexcept
    {
        return HOST_SPECIAL_KEY_NAMES.name(key);
    }

    [[nodiscard]] static std::string_view terminal_input(const mnos::host::HostSpecialKey key) noexcept
    {
        return HOST_SPECIAL_KEY_TERMINAL_INPUTS.value_or(key, HOST_TERMINAL_NO_TERMINAL_INPUT);
    }

private:
    inline static constexpr auto HOST_SPECIAL_KEY_NAMES =
        mnos::core::make_enum_name_table<mnos::host::HostSpecialKey>(
            std::array<std::string_view, mnos::host::HOST_SPECIAL_KEY_COUNT>{
                "ENTER",
                "BACKSPACE",
                "DELETE_KEY",
                "TAB",
                "ESCAPE",
                "ARROW_UP",
                "ARROW_DOWN",
                "ARROW_LEFT",
                "ARROW_RIGHT",
                "CTRL_C",
                "CTRL_D"},
            HOST_TERMINAL_ENUM_INVALID_NAME);
    inline static constexpr auto HOST_SPECIAL_KEY_TERMINAL_INPUTS =
        mnos::core::make_enum_map<mnos::host::HostSpecialKey>(
            std::array<std::string_view, mnos::host::HOST_SPECIAL_KEY_COUNT>{
                HOST_TERMINAL_ENTER_INPUT,
                HOST_TERMINAL_BACKSPACE_INPUT,
                HOST_TERMINAL_DELETE_INPUT,
                HOST_TERMINAL_TAB_INPUT,
                HOST_TERMINAL_NO_TERMINAL_INPUT,
                HOST_TERMINAL_NO_TERMINAL_INPUT,
                HOST_TERMINAL_NO_TERMINAL_INPUT,
                HOST_TERMINAL_NO_TERMINAL_INPUT,
                HOST_TERMINAL_NO_TERMINAL_INPUT,
                HOST_TERMINAL_NO_TERMINAL_INPUT,
                HOST_TERMINAL_NO_TERMINAL_INPUT});
};

[[nodiscard]] std::size_t trimmed_line_size(
    const std::string_view line,
    const std::size_t cursor_column,
    const bool is_cursor_row) noexcept
{
    std::size_t line_size = line.size();
    while (line_size > std::size_t{0} &&
           line[line_size - std::size_t{1}] == dev::TERMINAL_BLANK_CHARACTER)
    {
        --line_size;
    }

    if (is_cursor_row)
    {
        line_size = std::max(line_size, std::min(cursor_column, line.size()));
    }
    return line_size;
}

[[nodiscard]] std::string trimmed_display_line(
    std::string line,
    const std::size_t cursor_column,
    const bool is_cursor_row)
{
    line.resize(trimmed_line_size(line, cursor_column, is_cursor_row));
    return line;
}

[[nodiscard]] std::string render_display_for_host(const dev::TextDisplayBuffer& display)
{
    std::vector<std::string> trimmed_lines;
    trimmed_lines.reserve(display.row_count());

    std::size_t last_visible_row = std::size_t{0};
    bool has_visible_row = false;
    for (std::size_t row = std::size_t{0}; row < display.row_count(); ++row)
    {
        const bool is_cursor_row = row == display.cursor_row();
        std::string line = trimmed_display_line(display.line(row), display.cursor_column(), is_cursor_row);
        if (!line.empty())
        {
            last_visible_row = row;
            has_visible_row = true;
        }
        trimmed_lines.push_back(std::move(line));
    }

    if (!has_visible_row)
    {
        return {};
    }

    std::string rendered;
    for (std::size_t row = std::size_t{0}; row <= last_visible_row; ++row)
    {
        if (row != std::size_t{0})
        {
            rendered.push_back(HOST_TERMINAL_NEWLINE_CHARACTER);
        }
        rendered.append(trimmed_lines[row]);
    }
    return rendered;
}

[[nodiscard]] std::string render_output_stream_for_host(
    const std::string_view output_stream,
    const mnos::host::TerminalRenderMode mode)
{
    std::string rendered;
    rendered.reserve(output_stream.size());
    for (const char character : output_stream)
    {
        if (character == dev::TERMINAL_CLEAR_SCREEN_CHARACTER)
        {
            if (mnos::host::terminal_render_mode_uses_ansi(mode))
            {
                rendered.append(HOST_TERMINAL_ANSI_CLEAR_SCREEN);
                rendered.append(HOST_TERMINAL_ANSI_CURSOR_HOME);
            }
            continue;
        }
        if (character == dev::TERMINAL_BACKSPACE_CHARACTER || character == dev::TERMINAL_DELETE_CHARACTER)
        {
            rendered.append(HOST_TERMINAL_BACKSPACE_ERASE);
            continue;
        }
        rendered.push_back(character);
    }
    return rendered;
}

[[nodiscard]] bool render_terminal_stream(
    dev::TerminalDevice& terminal,
    const mnos::host::TerminalRenderMode mode,
    std::ostream& output,
    std::size_t& render_count)
{
    const std::string_view pending_output = terminal.output_stream_since(std::size_t{0});
    if (pending_output.empty())
    {
        return true;
    }

    const std::string rendered = render_output_stream_for_host(pending_output, mode);
    if (!rendered.empty())
    {
        output << rendered;
        output.flush();
        if (!output.good())
        {
            return false;
        }
        ++render_count;
    }
    terminal.discard_output_stream_before(pending_output.size());
    return true;
}

[[nodiscard]] bool render_terminal_screen(
    const dev::TerminalDevice& terminal,
    const mnos::host::TerminalRenderMode mode,
    std::ostream& output,
    std::string& last_screen_frame,
    std::size_t& render_count)
{
    const std::string rendered = render_display_for_host(terminal.display());
    if (rendered == last_screen_frame)
    {
        return true;
    }

    last_screen_frame = rendered;
    if (mnos::host::terminal_render_mode_uses_ansi(mode))
    {
        output << HOST_TERMINAL_ANSI_CLEAR_SCREEN << HOST_TERMINAL_ANSI_CURSOR_HOME << rendered;
        output.flush();
    }
    else
    {
        output << rendered << HOST_TERMINAL_NEWLINE_CHARACTER;
    }

    if (!output.good())
    {
        return false;
    }

    ++render_count;
    return true;
}

void append_host_line_ending(std::string& line)
{
    if (!line.empty() && line.back() == HOST_TERMINAL_CARRIAGE_RETURN_CHARACTER)
    {
        line.pop_back();
    }
    line.push_back(HOST_TERMINAL_NEWLINE_CHARACTER);
}
}

namespace mnos::host
{
bool terminal_render_mode_is_screen(const TerminalRenderMode mode) noexcept
{
    return mode == TerminalRenderMode::ANSI_SCREEN || mode == TerminalRenderMode::PLAIN_SCREEN;
}

bool terminal_render_mode_uses_ansi(const TerminalRenderMode mode) noexcept
{
    return mode == TerminalRenderMode::ANSI_STREAM || mode == TerminalRenderMode::ANSI_SCREEN;
}

bool is_host_input_event_kind_valid(const HostInputEventKind kind) noexcept
{
    return HostInputEventKindCatalog::contains(kind);
}

std::size_t host_input_event_kind_to_index(const HostInputEventKind kind) noexcept
{
    return HostInputEventKindCatalog::index(kind);
}

std::string_view host_input_event_kind_to_name(const HostInputEventKind kind) noexcept
{
    return HostInputEventKindCatalog::name(kind);
}

bool is_host_special_key_valid(const HostSpecialKey key) noexcept
{
    return HostSpecialKeyCatalog::contains(key);
}

std::size_t host_special_key_to_index(const HostSpecialKey key) noexcept
{
    return HostSpecialKeyCatalog::index(key);
}

std::string_view host_special_key_to_name(const HostSpecialKey key) noexcept
{
    return HostSpecialKeyCatalog::name(key);
}

std::string_view host_special_key_to_terminal_input(const HostSpecialKey key) noexcept
{
    return HostSpecialKeyCatalog::terminal_input(key);
}

HostInputEvent HostInputEvent::text(std::string input_text) noexcept
{
    return HostInputEvent{HostInputEventKind::TEXT, std::move(input_text), HostSpecialKey::COUNT};
}

HostInputEvent HostInputEvent::special_key(const HostSpecialKey key) noexcept
{
    return HostInputEvent{HostInputEventKind::SPECIAL_KEY, {}, key};
}

HostInputEvent HostInputEvent::input_closed() noexcept
{
    return HostInputEvent{HostInputEventKind::INPUT_CLOSED, {}, HostSpecialKey::COUNT};
}

HostInputEvent HostInputEvent::host_io_error() noexcept
{
    return HostInputEvent{HostInputEventKind::HOST_IO_ERROR, {}, HostSpecialKey::COUNT};
}

HostInputEvent::HostInputEvent(
    const HostInputEventKind kind,
    std::string input_text,
    const HostSpecialKey special_key) noexcept :
    kind_(kind),
    text_(std::move(input_text)),
    special_key_(special_key)
{
}

HostInputEventKind HostInputEvent::kind() const noexcept
{
    return this->kind_;
}

std::string_view HostInputEvent::text() const noexcept
{
    return this->text_;
}

HostSpecialKey HostInputEvent::special_key() const noexcept
{
    return this->special_key_;
}

bool HostInputEvent::has_text() const noexcept
{
    return this->kind_ == HostInputEventKind::TEXT;
}

bool HostInputEvent::has_special_key() const noexcept
{
    return this->kind_ == HostInputEventKind::SPECIAL_KEY;
}

std::string host_input_event_to_terminal_input(const HostInputEvent& event)
{
    if (event.has_text())
    {
        return std::string{event.text()};
    }
    if (event.has_special_key())
    {
        return std::string{host_special_key_to_terminal_input(event.special_key())};
    }
    return {};
}

HostTerminalRenderer::HostTerminalRenderer(const TerminalRenderMode mode) noexcept : mode_(mode)
{
}

TerminalRenderMode HostTerminalRenderer::mode() const noexcept
{
    return this->mode_;
}

bool HostTerminalRenderer::render_if_changed(dev::TerminalDevice& terminal, std::ostream& output)
{
    if (terminal_render_mode_is_screen(this->mode_))
    {
        return render_terminal_screen(
            terminal,
            this->mode_,
            output,
            this->last_screen_frame_,
            this->render_count_);
    }
    return render_terminal_stream(terminal, this->mode_, output, this->render_count_);
}

std::size_t HostTerminalRenderer::render_count() const noexcept
{
    return this->render_count_;
}

StreamTerminalBackend::StreamTerminalBackend(
    std::istream& input,
    std::ostream& output,
    const TerminalRenderMode render_mode) noexcept :
    input_(&input),
    output_(&output),
    renderer_(render_mode)
{
}

TerminalRenderMode StreamTerminalBackend::render_mode() const noexcept
{
    return this->renderer_.mode();
}

HostInputEvent StreamTerminalBackend::read_input_event()
{
    std::string line;
    if (std::getline(*this->input_, line))
    {
        append_host_line_ending(line);
        return HostInputEvent::text(std::move(line));
    }

    if (this->input_->bad())
    {
        return HostInputEvent::host_io_error();
    }
    return HostInputEvent::input_closed();
}

bool StreamTerminalBackend::render_terminal(dev::TerminalDevice& terminal)
{
    return this->renderer_.render_if_changed(terminal, *this->output_);
}

std::size_t StreamTerminalBackend::render_count() const noexcept
{
    return this->renderer_.render_count();
}
}
