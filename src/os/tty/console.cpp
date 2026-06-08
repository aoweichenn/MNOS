#include <algorithm>

#include <mnos/os/tty/console.hpp>

namespace
{
constexpr std::string_view CONSOLE_BACKSPACE_ECHO = "\b";
constexpr std::string_view CONSOLE_NEWLINE_ECHO = "\n";
}

namespace mnos::os::tty
{
ConsoleReadResult ConsoleReadResult::ready(const std::size_t byte_count) noexcept
{
    return ConsoleReadResult{ConsoleReadStatus::READY, byte_count};
}

ConsoleReadResult ConsoleReadResult::blocked() noexcept
{
    return ConsoleReadResult{ConsoleReadStatus::BLOCKED, std::size_t{0}};
}

ConsoleReadResult::ConsoleReadResult(const ConsoleReadStatus status, const std::size_t byte_count) noexcept :
    status_(status),
    byte_count_(byte_count)
{
}

ConsoleReadStatus ConsoleReadResult::status() const noexcept
{
    return this->status_;
}

std::size_t ConsoleReadResult::byte_count() const noexcept
{
    return this->byte_count_;
}

bool ConsoleReadResult::is_ready() const noexcept
{
    return this->status_ == ConsoleReadStatus::READY;
}

bool ConsoleReadResult::is_blocked() const noexcept
{
    return this->status_ == ConsoleReadStatus::BLOCKED;
}

Console::Console(dev::TerminalDevice& terminal) noexcept : terminal_(&terminal)
{
}

dev::TerminalDevice& Console::terminal() noexcept
{
    return *this->terminal_;
}

const dev::TerminalDevice& Console::terminal() const noexcept
{
    return *this->terminal_;
}

bool Console::has_ready_input() const noexcept
{
    return !this->ready_input_.empty();
}

std::size_t Console::ready_input_size() const noexcept
{
    return this->ready_input_.size();
}

std::size_t Console::current_line_size() const noexcept
{
    return this->current_line_.size();
}

std::size_t Console::waiting_reader_count() const noexcept
{
    return this->input_waiters_.size();
}

void Console::clear()
{
    this->terminal_->display().clear();
    this->terminal_->keyboard().clear();
    this->current_line_.clear();
    this->ready_input_.clear();
}

void Console::write(const std::string_view text)
{
    this->terminal_->write_output(text);
}

ConsoleReadResult Console::read(std::span<char> destination, sched::ThreadContext& thread)
{
    if (!this->ready_input_.empty() || destination.empty())
    {
        return ConsoleReadResult::ready(this->copy_ready_input(destination));
    }

    if (!this->input_waiters_.contains(thread))
    {
        this->input_waiters_.wait(thread);
    }
    return ConsoleReadResult::blocked();
}

std::vector<sched::ThreadContext*> Console::submit_input(const std::string_view text)
{
    static_cast<void>(this->terminal_->submit_input(text));
    std::size_t completed_line_count = std::size_t{0};
    for (const dev::KeyEvent event : this->terminal_->drain_input())
    {
        if (this->handle_key_event(event))
        {
            ++completed_line_count;
        }
    }
    return this->wake_ready_readers(completed_line_count);
}

bool Console::handle_key_event(const dev::KeyEvent event)
{
    if (event.is_enter())
    {
        return this->handle_enter();
    }
    if (event.is_backspace())
    {
        this->handle_backspace();
        return false;
    }
    if (event.is_printable_ascii())
    {
        this->handle_printable(event);
    }
    return false;
}

void Console::handle_printable(const dev::KeyEvent event)
{
    this->current_line_.push_back(event.character());
    this->terminal_->write_output(std::string_view{&this->current_line_.back(), std::size_t{1}});
}

bool Console::handle_enter()
{
    for (const char character : this->current_line_)
    {
        this->ready_input_.push_back(character);
    }
    this->ready_input_.push_back(dev::TERMINAL_NEWLINE_CHARACTER);
    this->current_line_.clear();
    this->terminal_->write_output(CONSOLE_NEWLINE_ECHO);
    return true;
}

void Console::handle_backspace()
{
    if (this->current_line_.empty())
    {
        return;
    }
    this->current_line_.pop_back();
    this->terminal_->write_output(CONSOLE_BACKSPACE_ECHO);
}

std::size_t Console::copy_ready_input(std::span<char> destination)
{
    const std::size_t byte_count = std::min(destination.size(), this->ready_input_.size());
    for (std::size_t index = std::size_t{0}; index < byte_count; ++index)
    {
        destination[index] = this->ready_input_.front();
        this->ready_input_.pop_front();
    }
    return byte_count;
}

std::vector<sched::ThreadContext*> Console::wake_ready_readers(const std::size_t completed_line_count)
{
    std::vector<sched::ThreadContext*> readers;
    readers.reserve(completed_line_count);
    for (std::size_t line_index = std::size_t{0}; line_index < completed_line_count; ++line_index)
    {
        if (this->ready_input_.empty())
        {
            break;
        }
        sched::ThreadContext* const reader = this->input_waiters_.wake_one();
        if (reader == nullptr)
        {
            break;
        }
        readers.push_back(reader);
    }
    return readers;
}
}
