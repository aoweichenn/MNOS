#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace mnos::os::dev
{
class TerminalDevice;
}

namespace mnos::host
{
enum class TerminalRenderMode : std::uint8_t
{
    ANSI_STREAM,
    PLAIN_STREAM,
    ANSI_SCREEN,
    PLAIN_SCREEN,
    COUNT
};

enum class HostInputEventKind : std::uint8_t
{
    TEXT,
    SPECIAL_KEY,
    INPUT_CLOSED,
    HOST_IO_ERROR,
    COUNT
};

enum class HostSpecialKey : std::uint8_t
{
    ENTER,
    BACKSPACE,
    DELETE_KEY,
    TAB,
    ESCAPE,
    ARROW_UP,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    CTRL_C,
    CTRL_D,
    COUNT
};

inline constexpr std::size_t HOST_INPUT_EVENT_KIND_COUNT =
    static_cast<std::size_t>(HostInputEventKind::COUNT);
inline constexpr std::size_t HOST_SPECIAL_KEY_COUNT = static_cast<std::size_t>(HostSpecialKey::COUNT);

[[nodiscard]] bool terminal_render_mode_is_screen(TerminalRenderMode mode) noexcept;
[[nodiscard]] bool terminal_render_mode_uses_ansi(TerminalRenderMode mode) noexcept;

[[nodiscard]] bool is_host_input_event_kind_valid(HostInputEventKind kind) noexcept;
[[nodiscard]] std::size_t host_input_event_kind_to_index(HostInputEventKind kind) noexcept;
[[nodiscard]] std::string_view host_input_event_kind_to_name(HostInputEventKind kind) noexcept;

[[nodiscard]] bool is_host_special_key_valid(HostSpecialKey key) noexcept;
[[nodiscard]] std::size_t host_special_key_to_index(HostSpecialKey key) noexcept;
[[nodiscard]] std::string_view host_special_key_to_name(HostSpecialKey key) noexcept;
[[nodiscard]] std::string_view host_special_key_to_terminal_input(HostSpecialKey key) noexcept;

class HostInputEvent final
{
public:
    [[nodiscard]] static HostInputEvent text(std::string input_text) noexcept;
    [[nodiscard]] static HostInputEvent special_key(HostSpecialKey key) noexcept;
    [[nodiscard]] static HostInputEvent input_closed() noexcept;
    [[nodiscard]] static HostInputEvent host_io_error() noexcept;

    [[nodiscard]] HostInputEventKind kind() const noexcept;
    [[nodiscard]] std::string_view text() const noexcept;
    [[nodiscard]] HostSpecialKey special_key() const noexcept;
    [[nodiscard]] bool has_text() const noexcept;
    [[nodiscard]] bool has_special_key() const noexcept;

private:
    HostInputEvent(HostInputEventKind kind, std::string input_text, HostSpecialKey special_key) noexcept;

    HostInputEventKind kind_;
    std::string text_;
    HostSpecialKey special_key_;
};

[[nodiscard]] std::string host_input_event_to_terminal_input(const HostInputEvent& event);

class HostTerminalRenderer final
{
public:
    explicit HostTerminalRenderer(TerminalRenderMode mode) noexcept;

    [[nodiscard]] TerminalRenderMode mode() const noexcept;
    [[nodiscard]] bool render_if_changed(os::dev::TerminalDevice& terminal, std::ostream& output);
    [[nodiscard]] std::size_t render_count() const noexcept;

private:
    TerminalRenderMode mode_;
    std::string last_screen_frame_;
    std::size_t render_count_ = std::size_t{0};
};

class HostTerminalBackend
{
public:
    HostTerminalBackend() = default;
    HostTerminalBackend(const HostTerminalBackend&) = delete;
    HostTerminalBackend& operator=(const HostTerminalBackend&) = delete;
    HostTerminalBackend(HostTerminalBackend&&) = delete;
    HostTerminalBackend& operator=(HostTerminalBackend&&) = delete;
    virtual ~HostTerminalBackend() = default;

    [[nodiscard]] virtual HostInputEvent read_input_event() = 0;
    [[nodiscard]] virtual bool render_terminal(os::dev::TerminalDevice& terminal) = 0;
    [[nodiscard]] virtual std::size_t render_count() const noexcept = 0;
};

class StreamTerminalBackend final : public HostTerminalBackend
{
public:
    StreamTerminalBackend(std::istream& input, std::ostream& output, TerminalRenderMode render_mode) noexcept;

    [[nodiscard]] TerminalRenderMode render_mode() const noexcept;
    [[nodiscard]] HostInputEvent read_input_event() override;
    [[nodiscard]] bool render_terminal(os::dev::TerminalDevice& terminal) override;
    [[nodiscard]] std::size_t render_count() const noexcept override;

private:
    std::istream* input_;
    std::ostream* output_;
    HostTerminalRenderer renderer_;
};
}
