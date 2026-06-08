#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mnos/os/dev/terminal.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/sched/wait_queue.hpp>

namespace mnos::os::tty
{
enum class ConsoleReadStatus : std::uint8_t
{
    READY,
    BLOCKED,
    COUNT
};

class ConsoleReadResult final
{
public:
    [[nodiscard]] static ConsoleReadResult ready(std::size_t byte_count) noexcept;
    [[nodiscard]] static ConsoleReadResult blocked() noexcept;

    [[nodiscard]] ConsoleReadStatus status() const noexcept;
    [[nodiscard]] std::size_t byte_count() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_blocked() const noexcept;

private:
    ConsoleReadResult(ConsoleReadStatus status, std::size_t byte_count) noexcept;

    ConsoleReadStatus status_;
    std::size_t byte_count_;
};

class Console final
{
public:
    explicit Console(dev::TerminalDevice& terminal) noexcept;

    [[nodiscard]] dev::TerminalDevice& terminal() noexcept;
    [[nodiscard]] const dev::TerminalDevice& terminal() const noexcept;
    [[nodiscard]] bool has_ready_input() const noexcept;
    [[nodiscard]] std::size_t ready_input_size() const noexcept;
    [[nodiscard]] std::size_t current_line_size() const noexcept;
    [[nodiscard]] std::size_t waiting_reader_count() const noexcept;

    void clear();
    void write(std::string_view text);
    [[nodiscard]] ConsoleReadResult read(std::span<char> destination, sched::ThreadContext& thread);
    [[nodiscard]] std::vector<sched::ThreadContext*> submit_input(std::string_view text);

private:
    [[nodiscard]] bool handle_key_event(dev::KeyEvent event);
    void handle_printable(dev::KeyEvent event);
    [[nodiscard]] bool handle_enter();
    void handle_backspace();
    [[nodiscard]] std::size_t copy_ready_input(std::span<char> destination);
    [[nodiscard]] std::vector<sched::ThreadContext*> wake_ready_readers(std::size_t completed_line_count);

    dev::TerminalDevice* terminal_;
    std::string current_line_;
    std::deque<char> ready_input_;
    sched::WaitQueue input_waiters_;
};
}
