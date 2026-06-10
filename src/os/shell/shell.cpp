#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/os/fs/vfs.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/sched/thread_state.hpp>
#include <mnos/os/shell/shell.hpp>

namespace
{
constexpr std::size_t SHELL_BUILTIN_COUNT = std::size_t{14};
constexpr std::string_view SHELL_UNKNOWN_COMMAND_PREFIX = "unknown command: ";
constexpr std::string_view SHELL_PARSE_ERROR_UNTERMINATED_QUOTE = "parse error: unterminated quote\n";
constexpr std::string_view SHELL_ARGUMENT_SEPARATOR = " ";
constexpr std::string_view SHELL_LINE_ENDING = "\n";
constexpr std::string_view SHELL_USAGE_PREFIX = "usage: ";
constexpr std::string_view SHELL_DESCRIPTION_PREFIX = "description: ";
constexpr std::string_view SHELL_NO_PARENT_TEXT = "0";
constexpr std::string_view SHELL_NO_EXIT_CODE_TEXT = "-";
constexpr std::string_view SHELL_ROOT_PATH = "/";
constexpr std::string_view SHELL_DIRECTORY_SUFFIX = "/";
constexpr std::string_view SHELL_HELP_CATALOG_HEADING = "commands:";
constexpr std::string_view SHELL_HELP_ITEM_INDENT = "  ";
constexpr std::string_view SHELL_HELP_DESCRIPTION_SEPARATOR = " - ";
constexpr std::string_view SHELL_SYNTAX_HELP = "help [command]";
constexpr std::string_view SHELL_SYNTAX_CLEAR = "clear";
constexpr std::string_view SHELL_SYNTAX_ECHO = "echo [text...]";
constexpr std::string_view SHELL_SYNTAX_PS = "ps";
constexpr std::string_view SHELL_SYNTAX_MEM = "mem";
constexpr std::string_view SHELL_SYNTAX_CPU = "cpu";
constexpr std::string_view SHELL_SYNTAX_TICKS = "ticks";
constexpr std::string_view SHELL_SYNTAX_LS = "ls [path]";
constexpr std::string_view SHELL_SYNTAX_CAT = "cat path";
constexpr std::string_view SHELL_SYNTAX_TOUCH = "touch path";
constexpr std::string_view SHELL_SYNTAX_WRITE = "write path text...";
constexpr std::string_view SHELL_SYNTAX_STAT = "stat path";
constexpr std::string_view SHELL_SYNTAX_RUN = "run path [args...] [--max-steps=N|--max-steps N]";
constexpr std::string_view SHELL_SYNTAX_EXIT = "exit";
constexpr std::string_view SHELL_DESCRIPTION_HELP = "show all commands or details for one command";
constexpr std::string_view SHELL_DESCRIPTION_CLEAR = "clear the terminal display";
constexpr std::string_view SHELL_DESCRIPTION_ECHO = "print arguments to the terminal";
constexpr std::string_view SHELL_DESCRIPTION_PS = "list processes and thread states";
constexpr std::string_view SHELL_DESCRIPTION_MEM = "show physical page allocator counters";
constexpr std::string_view SHELL_DESCRIPTION_CPU = "show current shell CPU state";
constexpr std::string_view SHELL_DESCRIPTION_TICKS = "show kernel scheduler tick counters";
constexpr std::string_view SHELL_DESCRIPTION_LS = "list directory entries";
constexpr std::string_view SHELL_DESCRIPTION_CAT = "print file contents";
constexpr std::string_view SHELL_DESCRIPTION_TOUCH = "create an empty file";
constexpr std::string_view SHELL_DESCRIPTION_WRITE = "replace file contents with text";
constexpr std::string_view SHELL_DESCRIPTION_STAT = "show file kind and size";
constexpr std::string_view SHELL_DESCRIPTION_RUN = "load and execute an ELF64 user program";
constexpr std::string_view SHELL_DESCRIPTION_EXIT = "stop the interactive shell session";
constexpr std::string_view SHELL_FS_NOT_FOUND_PREFIX = "not found: ";
constexpr std::string_view SHELL_FS_INVALID_PATH_PREFIX = "invalid path: ";
constexpr std::string_view SHELL_FS_NOT_DIRECTORY_PREFIX = "not a directory: ";
constexpr std::string_view SHELL_FS_IS_DIRECTORY_PREFIX = "is a directory: ";
constexpr std::string_view SHELL_FS_NO_SPACE_PREFIX = "no space: ";
constexpr std::string_view SHELL_KIND_FILE = "file";
constexpr std::string_view SHELL_KIND_DIRECTORY = "directory";
constexpr std::string_view SHELL_KIND_UNKNOWN = "unknown";
constexpr std::string_view SHELL_RUN_UNAVAILABLE_TEXT = "run unavailable: shell has no process context\n";
constexpr std::string_view SHELL_RUN_ERROR_PREFIX = "run error: ";
constexpr std::string_view SHELL_RUN_INVALID_STEPS_TEXT = "run error: invalid max_steps\n";
constexpr std::string_view SHELL_RUN_INVALID_EXECUTABLE_PREFIX = "invalid executable: ";
constexpr std::string_view SHELL_RUN_PID_FIELD = " pid=";
constexpr std::string_view SHELL_RUN_STATUS_FIELD = " status=";
constexpr std::string_view SHELL_RUN_WAIT_FIELD = " wait=";
constexpr std::string_view SHELL_RUN_EXIT_FIELD = " exit=";
constexpr std::string_view SHELL_RUN_STEPS_FIELD = " steps=";
constexpr std::string_view SHELL_RUN_TRACE_FIELD = " trace=";
constexpr std::string_view SHELL_RUN_MAX_STEPS_OPTION = "--max-steps";
constexpr std::string_view SHELL_RUN_OPTION_TERMINATOR = "--";
constexpr char SHELL_RUN_OPTION_VALUE_SEPARATOR = '=';
constexpr const char* SHELL_COMMAND_MISSING_MESSAGE = "shell parse result does not contain a command";
constexpr const char* SHELL_ARGUMENT_INDEX_OUT_OF_RANGE_MESSAGE = "shell command argument index is out of range";
constexpr const char* SHELL_BUILTIN_INDEX_OUT_OF_RANGE_MESSAGE = "shell builtin index is out of range";
constexpr const char* SHELL_PROCESS_CONTEXT_MISSING_MESSAGE = "shell context does not contain a process";
constexpr const char* SHELL_THREAD_CONTEXT_MISSING_MESSAGE = "shell context does not contain a thread";

using BuiltinHandler = mnos::os::shell::ShellCommandResult (*)(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext&);

struct ShellBuiltinSpec final
{
    mnos::os::shell::ShellBuiltinInfo info;
    BuiltinHandler handler;
};

enum class ShellRunParseStatus : std::uint8_t
{
    READY,
    USAGE,
    INVALID_MAX_STEPS,
};

struct ShellRunRequest final
{
    std::string path;
    std::vector<std::string> arguments;
    std::size_t max_steps = mnos::os::kernel::KERNEL_USER_EXEC_DEFAULT_MAX_STEPS;
};

[[nodiscard]] bool shell_character_is_whitespace(const char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

[[nodiscard]] bool shell_character_is_quote(const char character) noexcept
{
    return character == '\'' || character == '"';
}

void shell_write(mnos::os::shell::ShellContext& context, const std::string_view text)
{
    context.os_kernel().console_write(text);
}

void shell_write_usage(mnos::os::shell::ShellContext& context, const std::string_view syntax)
{
    std::string output{SHELL_USAGE_PREFIX};
    output.append(syntax);
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
}

void shell_write_unknown_command(mnos::os::shell::ShellContext& context, const std::string_view name)
{
    std::string output{SHELL_UNKNOWN_COMMAND_PREFIX};
    output.append(name);
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
}

[[nodiscard]] std::string shell_join_arguments(const mnos::os::shell::ShellCommand& command)
{
    std::string output;
    for (std::size_t argument_index = std::size_t{0}; argument_index < command.argument_count(); ++argument_index)
    {
        if (argument_index != std::size_t{0})
        {
            output.append(SHELL_ARGUMENT_SEPARATOR);
        }
        output.append(command.argument_at(argument_index));
    }
    output.append(SHELL_LINE_ENDING);
    return output;
}

[[nodiscard]] std::string shell_join_arguments_from(
    const mnos::os::shell::ShellCommand& command,
    const std::size_t first_argument)
{
    std::string output;
    for (std::size_t argument_index = first_argument; argument_index < command.argument_count(); ++argument_index)
    {
        if (argument_index != first_argument)
        {
            output.append(SHELL_ARGUMENT_SEPARATOR);
        }
        output.append(command.argument_at(argument_index));
    }
    return output;
}

void shell_write_path_error(
    mnos::os::shell::ShellContext& context,
    const std::string_view prefix,
    const std::string_view path)
{
    std::string output{prefix};
    output.append(path);
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
}

void shell_write_run_error(
    mnos::os::shell::ShellContext& context,
    const std::string_view prefix,
    const std::string_view detail)
{
    std::string output{SHELL_RUN_ERROR_PREFIX};
    output.append(prefix);
    output.append(detail);
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
}

[[nodiscard]] std::string_view shell_node_kind_name(const mnos::os::fs::SimpleFsNodeKind kind) noexcept
{
    switch (kind)
    {
    case mnos::os::fs::SimpleFsNodeKind::FILE:
        return SHELL_KIND_FILE;
    case mnos::os::fs::SimpleFsNodeKind::DIRECTORY:
        return SHELL_KIND_DIRECTORY;
    case mnos::os::fs::SimpleFsNodeKind::COUNT:
        break;
    }
    return SHELL_KIND_UNKNOWN;
}

[[nodiscard]] std::vector<mnos::cpu::Byte> shell_bytes_from_text(const std::string_view text)
{
    std::vector<mnos::cpu::Byte> bytes;
    bytes.reserve(text.size());
    for (const char character : text)
    {
        bytes.push_back(static_cast<mnos::cpu::Byte>(static_cast<unsigned char>(character)));
    }
    return bytes;
}

[[nodiscard]] bool shell_parse_max_steps(const std::string_view text, std::size_t& max_steps) noexcept
{
    if (text.empty())
    {
        return false;
    }
    std::size_t parsed_value = std::size_t{0};
    const char* const first = text.data();
    const char* const last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed_value);
    if (result.ec != std::errc{} || result.ptr != last || parsed_value == std::size_t{0})
    {
        return false;
    }
    max_steps = parsed_value;
    return true;
}

[[nodiscard]] bool shell_text_is_unsigned_decimal(const std::string_view text) noexcept
{
    if (text.empty())
    {
        return false;
    }

    return std::ranges::all_of(
        text,
        [](const char character) noexcept
        {
            return character >= '0' && character <= '9';
        });
}

[[nodiscard]] bool shell_token_is_max_steps_option(const std::string_view token) noexcept
{
    return token == SHELL_RUN_MAX_STEPS_OPTION ||
           (token.starts_with(SHELL_RUN_MAX_STEPS_OPTION) &&
            token.size() > SHELL_RUN_MAX_STEPS_OPTION.size() &&
            token[SHELL_RUN_MAX_STEPS_OPTION.size()] == SHELL_RUN_OPTION_VALUE_SEPARATOR);
}

[[nodiscard]] bool shell_parse_max_steps_option(
    const mnos::os::shell::ShellCommand& command,
    std::size_t& argument_index,
    std::size_t& max_steps)
{
    const std::string_view token = command.argument_at(argument_index);
    if (token == SHELL_RUN_MAX_STEPS_OPTION)
    {
        const std::size_t value_index = argument_index + std::size_t{1};
        if (value_index >= command.argument_count() ||
            !shell_parse_max_steps(command.argument_at(value_index), max_steps))
        {
            return false;
        }
        argument_index += std::size_t{2};
        return true;
    }

    const std::size_t assignment_prefix_size = SHELL_RUN_MAX_STEPS_OPTION.size() + std::size_t{1};
    if (token.size() >= assignment_prefix_size &&
        token.starts_with(SHELL_RUN_MAX_STEPS_OPTION) &&
        token[SHELL_RUN_MAX_STEPS_OPTION.size()] == SHELL_RUN_OPTION_VALUE_SEPARATOR)
    {
        const std::string_view value = token.substr(assignment_prefix_size);
        if (!shell_parse_max_steps(value, max_steps))
        {
            return false;
        }
        ++argument_index;
        return true;
    }

    return false;
}

[[nodiscard]] ShellRunParseStatus shell_parse_run_request(
    const mnos::os::shell::ShellCommand& command,
    ShellRunRequest& request)
{
    if (command.argument_count() < std::size_t{1})
    {
        return ShellRunParseStatus::USAGE;
    }

    request.path = std::string{command.argument_at(std::size_t{0})};
    request.arguments.clear();
    request.arguments.push_back(request.path);

    if (command.argument_count() == std::size_t{2} &&
        shell_text_is_unsigned_decimal(command.argument_at(std::size_t{1})))
    {
        return shell_parse_max_steps(command.argument_at(std::size_t{1}), request.max_steps)
            ? ShellRunParseStatus::READY
            : ShellRunParseStatus::INVALID_MAX_STEPS;
    }

    bool parse_options = true;
    for (std::size_t argument_index = std::size_t{1}; argument_index < command.argument_count();)
    {
        const std::string_view token = command.argument_at(argument_index);
        if (parse_options && token == SHELL_RUN_OPTION_TERMINATOR)
        {
            parse_options = false;
            ++argument_index;
            continue;
        }

        if (parse_options && shell_token_is_max_steps_option(token))
        {
            if (!shell_parse_max_steps_option(command, argument_index, request.max_steps))
            {
                return ShellRunParseStatus::INVALID_MAX_STEPS;
            }
            continue;
        }

        request.arguments.emplace_back(token);
        ++argument_index;
    }

    return ShellRunParseStatus::READY;
}

[[nodiscard]] bool shell_run_status_should_wait(const mnos::os::kernel::UserProcessRunStatus status) noexcept
{
    switch (status)
    {
    case mnos::os::kernel::UserProcessRunStatus::EXITED:
    case mnos::os::kernel::UserProcessRunStatus::KILLED:
        return true;
    case mnos::os::kernel::UserProcessRunStatus::BLOCKED:
    case mnos::os::kernel::UserProcessRunStatus::OUT_OF_MEMORY:
    case mnos::os::kernel::UserProcessRunStatus::MAX_STEPS:
    case mnos::os::kernel::UserProcessRunStatus::COUNT:
        break;
    }
    return false;
}

[[nodiscard]] std::string shell_run_result_text(
    const std::string_view path,
    const mnos::os::kernel::UserProcessRunResult& run_result,
    const std::optional<mnos::os::kernel::ProcessWaitResult>& wait_result)
{
    std::string output{"run "};
    output.append(path);
    output.append(SHELL_RUN_PID_FIELD);
    output.append(std::to_string(run_result.process_id().value()));
    output.append(SHELL_RUN_STATUS_FIELD);
    output.append(mnos::os::kernel::user_process_run_status_to_name(run_result.status()));
    if (wait_result.has_value())
    {
        output.append(SHELL_RUN_WAIT_FIELD);
        output.append(mnos::os::kernel::process_wait_status_to_name(wait_result->status()));
    }
    if (run_result.has_exit_code())
    {
        output.append(SHELL_RUN_EXIT_FIELD);
        output.append(std::to_string(run_result.exit_code()));
    }
    output.append(SHELL_RUN_STEPS_FIELD);
    output.append(std::to_string(run_result.executed_step_count()));
    output.append(SHELL_RUN_TRACE_FIELD);
    output.append(std::to_string(run_result.trace().size()));
    output.append(SHELL_LINE_ENDING);
    return output;
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_help(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_clear(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_echo(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ps(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_mem(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_cpu(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ticks(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ls(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_cat(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_touch(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_write(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_stat(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_run(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_exit(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);

[[nodiscard]] const std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT>& shell_builtin_catalog() noexcept
{
    static constexpr std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT> CATALOG{
        ShellBuiltinSpec{{"help", SHELL_SYNTAX_HELP, SHELL_DESCRIPTION_HELP}, handle_help},
        ShellBuiltinSpec{{"clear", SHELL_SYNTAX_CLEAR, SHELL_DESCRIPTION_CLEAR}, handle_clear},
        ShellBuiltinSpec{{"echo", SHELL_SYNTAX_ECHO, SHELL_DESCRIPTION_ECHO}, handle_echo},
        ShellBuiltinSpec{{"ps", SHELL_SYNTAX_PS, SHELL_DESCRIPTION_PS}, handle_ps},
        ShellBuiltinSpec{{"mem", SHELL_SYNTAX_MEM, SHELL_DESCRIPTION_MEM}, handle_mem},
        ShellBuiltinSpec{{"cpu", SHELL_SYNTAX_CPU, SHELL_DESCRIPTION_CPU}, handle_cpu},
        ShellBuiltinSpec{{"ticks", SHELL_SYNTAX_TICKS, SHELL_DESCRIPTION_TICKS}, handle_ticks},
        ShellBuiltinSpec{{"ls", SHELL_SYNTAX_LS, SHELL_DESCRIPTION_LS}, handle_ls},
        ShellBuiltinSpec{{"cat", SHELL_SYNTAX_CAT, SHELL_DESCRIPTION_CAT}, handle_cat},
        ShellBuiltinSpec{{"touch", SHELL_SYNTAX_TOUCH, SHELL_DESCRIPTION_TOUCH}, handle_touch},
        ShellBuiltinSpec{{"write", SHELL_SYNTAX_WRITE, SHELL_DESCRIPTION_WRITE}, handle_write},
        ShellBuiltinSpec{{"stat", SHELL_SYNTAX_STAT, SHELL_DESCRIPTION_STAT}, handle_stat},
        ShellBuiltinSpec{{"run", SHELL_SYNTAX_RUN, SHELL_DESCRIPTION_RUN}, handle_run},
        ShellBuiltinSpec{{"exit", SHELL_SYNTAX_EXIT, SHELL_DESCRIPTION_EXIT}, handle_exit}};
    return CATALOG;
}

[[nodiscard]] const ShellBuiltinSpec* shell_find_builtin(const std::string_view name) noexcept
{
    const std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT>& catalog = shell_builtin_catalog();
    const auto builtin = std::ranges::find_if(
        catalog,
        [name](const ShellBuiltinSpec& spec) noexcept
        {
            return spec.info.name == name;
        });
    return builtin == catalog.end() ? nullptr : &*builtin;
}

void append_shell_usage_line(std::string& output, const std::string_view syntax)
{
    output.append(SHELL_USAGE_PREFIX);
    output.append(syntax);
    output.append(SHELL_LINE_ENDING);
}

[[nodiscard]] std::string shell_help_detail_text(const mnos::os::shell::ShellBuiltinInfo& info)
{
    std::string output;
    append_shell_usage_line(output, info.syntax);
    output.append(SHELL_DESCRIPTION_PREFIX);
    output.append(info.description);
    output.append(SHELL_LINE_ENDING);
    return output;
}

[[nodiscard]] std::string shell_help_catalog_text()
{
    std::string output{"builtins:"};
    for (const ShellBuiltinSpec& builtin : shell_builtin_catalog())
    {
        output.append(SHELL_ARGUMENT_SEPARATOR);
        output.append(builtin.info.name);
    }
    output.append(SHELL_LINE_ENDING);
    output.append(SHELL_HELP_CATALOG_HEADING);
    output.append(SHELL_LINE_ENDING);
    for (const ShellBuiltinSpec& builtin : shell_builtin_catalog())
    {
        output.append(SHELL_HELP_ITEM_INDENT);
        output.append(SHELL_USAGE_PREFIX);
        output.append(builtin.info.syntax);
        output.append(SHELL_HELP_DESCRIPTION_SEPARATOR);
        output.append(builtin.info.description);
        output.append(SHELL_LINE_ENDING);
    }
    return output;
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_help(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    if (command.argument_count() == std::size_t{0})
    {
        shell_write(context, shell_help_catalog_text());
        return mnos::os::shell::ShellCommandResult::handled();
    }
    if (command.argument_count() != std::size_t{1})
    {
        shell_write_usage(context, SHELL_SYNTAX_HELP);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    const ShellBuiltinSpec* const builtin = shell_find_builtin(command.argument_at(std::size_t{0}));
    if (builtin == nullptr)
    {
        shell_write_unknown_command(context, command.argument_at(std::size_t{0}));
        return mnos::os::shell::ShellCommandResult::unknown_command();
    }

    shell_write(context, shell_help_detail_text(builtin->info));
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_clear(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    context.os_kernel().console().clear();
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_echo(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    shell_write(context, shell_join_arguments(command));
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ps(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    mnos::os::kernel::Kernel& os_kernel = context.os_kernel();
    std::string output{"pid ppid state exit threads states\n"};
    for (std::size_t process_index = std::size_t{0}; process_index < os_kernel.process_count(); ++process_index)
    {
        const mnos::os::proc::Process& process = os_kernel.process_at(process_index);
        output.append(std::to_string(process.id().value()));
        output.append(SHELL_ARGUMENT_SEPARATOR);
        if (process.has_parent())
        {
            output.append(std::to_string(process.parent_id().value()));
        }
        else
        {
            output.append(SHELL_NO_PARENT_TEXT);
        }
        output.append(SHELL_ARGUMENT_SEPARATOR);
        output.append(mnos::os::proc::process_state_to_name(process.state()));
        output.append(SHELL_ARGUMENT_SEPARATOR);
        if (process.is_exited() || process.is_reaped())
        {
            output.append(std::to_string(process.exit_code()));
        }
        else
        {
            output.append(SHELL_NO_EXIT_CODE_TEXT);
        }
        output.append(SHELL_ARGUMENT_SEPARATOR);
        output.append(std::to_string(process.thread_count()));
        output.append(SHELL_ARGUMENT_SEPARATOR);
        for (std::size_t thread_index = std::size_t{0}; thread_index < process.thread_count(); ++thread_index)
        {
            if (thread_index != std::size_t{0})
            {
                output.push_back(',');
            }
            output.append(mnos::os::sched::thread_state_to_name(process.thread_at(thread_index).state()));
        }
        output.append(SHELL_LINE_ENDING);
    }
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_mem(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    const mnos::os::mm::PhysicalPageAllocator& allocator = context.os_kernel().physical_page_allocator();
    std::string output{"memory_pages total="};
    output.append(std::to_string(allocator.total_page_count()));
    output.append(" free=");
    output.append(std::to_string(allocator.free_page_count()));
    output.append(" allocated=");
    output.append(std::to_string(allocator.allocated_page_count()));
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_cpu(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    const mnos::os::kernel::Kernel& os_kernel = context.os_kernel();
    std::string output{"cores="};
    output.append(std::to_string(os_kernel.bootstrap_processor_count()));
    output.append(" ready_threads=");
    output.append(std::to_string(os_kernel.scheduler().ready_count()));
    output.append(" running=");
    output.append(os_kernel.scheduler().has_current() ? "1" : "0");
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ticks(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    std::string output{"ticks="};
    output.append(std::to_string(context.os_kernel().scheduler_tick_count()));
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ls(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    if (command.argument_count() > std::size_t{1})
    {
        shell_write_usage(context, SHELL_SYNTAX_LS);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    const std::string_view path =
        command.argument_count() == std::size_t{0} ? SHELL_ROOT_PATH : command.argument_at(std::size_t{0});
    try
    {
        const std::optional<mnos::os::fs::VfsNode> node = context.os_kernel().vfs().lookup(path);
        if (!node.has_value())
        {
            shell_write_path_error(context, SHELL_FS_NOT_FOUND_PREFIX, path);
            return mnos::os::shell::ShellCommandResult::handled();
        }
        if (!node->is_directory())
        {
            shell_write_path_error(context, SHELL_FS_NOT_DIRECTORY_PREFIX, path);
            return mnos::os::shell::ShellCommandResult::handled();
        }

        std::string output;
        for (const mnos::os::fs::SimpleFsDirectoryEntry& entry : context.os_kernel().vfs().read_directory(path))
        {
            output.append(entry.name());
            if (entry.is_directory())
            {
                output.append(SHELL_DIRECTORY_SUFFIX);
            }
            output.append(SHELL_LINE_ENDING);
        }
        shell_write(context, output);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::invalid_argument&)
    {
        shell_write_path_error(context, SHELL_FS_INVALID_PATH_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_cat(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    if (command.argument_count() != std::size_t{1})
    {
        shell_write_usage(context, SHELL_SYNTAX_CAT);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    const std::string_view path = command.argument_at(std::size_t{0});
    try
    {
        const std::optional<mnos::os::fs::VfsNode> node = context.os_kernel().vfs().lookup(path);
        if (!node.has_value())
        {
            shell_write_path_error(context, SHELL_FS_NOT_FOUND_PREFIX, path);
            return mnos::os::shell::ShellCommandResult::handled();
        }
        if (node->is_directory())
        {
            shell_write_path_error(context, SHELL_FS_IS_DIRECTORY_PREFIX, path);
            return mnos::os::shell::ShellCommandResult::handled();
        }

        mnos::os::fs::VfsFile file =
            context.os_kernel().vfs().open_file(path, mnos::os::fs::VfsOpenMode::READ_ONLY);
        std::vector<mnos::cpu::Byte> bytes(static_cast<std::size_t>(file.size_bytes()));
        const std::size_t byte_count = file.read(bytes);
        std::string output;
        output.reserve(byte_count);
        for (std::size_t byte_index = std::size_t{0}; byte_index < byte_count; ++byte_index)
        {
            output.push_back(static_cast<char>(bytes[byte_index]));
        }
        shell_write(context, output);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::invalid_argument&)
    {
        shell_write_path_error(context, SHELL_FS_INVALID_PATH_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_touch(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    if (command.argument_count() != std::size_t{1})
    {
        shell_write_usage(context, SHELL_SYNTAX_TOUCH);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    const std::string_view path = command.argument_at(std::size_t{0});
    try
    {
        const std::optional<mnos::os::fs::VfsNode> node = context.os_kernel().vfs().lookup(path);
        if (node.has_value() && node->is_directory())
        {
            shell_write_path_error(context, SHELL_FS_IS_DIRECTORY_PREFIX, path);
            return mnos::os::shell::ShellCommandResult::handled();
        }
        if (!node.has_value())
        {
            static_cast<void>(context.os_kernel().vfs().create_file(path));
        }
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::length_error&)
    {
        shell_write_path_error(context, SHELL_FS_NO_SPACE_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::out_of_range&)
    {
        shell_write_path_error(context, SHELL_FS_NOT_FOUND_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::invalid_argument&)
    {
        shell_write_path_error(context, SHELL_FS_INVALID_PATH_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_write(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    if (command.argument_count() < std::size_t{2})
    {
        shell_write_usage(context, SHELL_SYNTAX_WRITE);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    const std::string_view path = command.argument_at(std::size_t{0});
    try
    {
        const std::optional<mnos::os::fs::VfsNode> node = context.os_kernel().vfs().lookup(path);
        if (node.has_value() && node->is_directory())
        {
            shell_write_path_error(context, SHELL_FS_IS_DIRECTORY_PREFIX, path);
            return mnos::os::shell::ShellCommandResult::handled();
        }
        if (!node.has_value())
        {
            static_cast<void>(context.os_kernel().vfs().create_file(path));
        }

        std::string text = shell_join_arguments_from(command, std::size_t{1});
        text.append(SHELL_LINE_ENDING);
        std::vector<mnos::cpu::Byte> bytes = shell_bytes_from_text(text);
        mnos::os::fs::VfsFile file =
            context.os_kernel().vfs().open_file(path, mnos::os::fs::VfsOpenMode::READ_WRITE);
        file.seek(file.size_bytes());
        static_cast<void>(file.write(bytes));
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::length_error&)
    {
        shell_write_path_error(context, SHELL_FS_NO_SPACE_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::out_of_range&)
    {
        shell_write_path_error(context, SHELL_FS_NOT_FOUND_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::invalid_argument&)
    {
        shell_write_path_error(context, SHELL_FS_INVALID_PATH_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_stat(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    if (command.argument_count() != std::size_t{1})
    {
        shell_write_usage(context, SHELL_SYNTAX_STAT);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    const std::string_view path = command.argument_at(std::size_t{0});
    try
    {
        const std::optional<mnos::os::fs::VfsNode> node = context.os_kernel().vfs().lookup(path);
        if (!node.has_value())
        {
            shell_write_path_error(context, SHELL_FS_NOT_FOUND_PREFIX, path);
            return mnos::os::shell::ShellCommandResult::handled();
        }

        std::string output{"path="};
        output.append(path);
        output.append(" kind=");
        output.append(shell_node_kind_name(node->kind()));
        output.append(" inode=");
        output.append(std::to_string(node->inode().value()));
        output.append(" size=");
        output.append(std::to_string(node->size_bytes()));
        output.append(SHELL_LINE_ENDING);
        shell_write(context, output);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::invalid_argument&)
    {
        shell_write_path_error(context, SHELL_FS_INVALID_PATH_PREFIX, path);
        return mnos::os::shell::ShellCommandResult::handled();
    }
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_run(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    ShellRunRequest request;
    const ShellRunParseStatus parse_status = shell_parse_run_request(command, request);
    if (parse_status == ShellRunParseStatus::USAGE)
    {
        shell_write_usage(context, SHELL_SYNTAX_RUN);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    if (parse_status == ShellRunParseStatus::INVALID_MAX_STEPS)
    {
        shell_write(context, SHELL_RUN_INVALID_STEPS_TEXT);
        return mnos::os::shell::ShellCommandResult::handled();
    }
    if (!context.has_process_context())
    {
        shell_write(context, SHELL_RUN_UNAVAILABLE_TEXT);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    try
    {
        const std::optional<mnos::os::fs::VfsNode> node = context.os_kernel().vfs().lookup(request.path);
        if (!node.has_value())
        {
            shell_write_path_error(context, SHELL_FS_NOT_FOUND_PREFIX, request.path);
            return mnos::os::shell::ShellCommandResult::handled();
        }
        if (node->is_directory())
        {
            shell_write_path_error(context, SHELL_FS_IS_DIRECTORY_PREFIX, request.path);
            return mnos::os::shell::ShellCommandResult::handled();
        }
    }
    catch (const std::invalid_argument&)
    {
        shell_write_path_error(context, SHELL_FS_INVALID_PATH_PREFIX, request.path);
        return mnos::os::shell::ShellCommandResult::handled();
    }

    try
    {
        const mnos::os::proc::UserProgramArguments program_arguments{std::move(request.arguments)};
        const mnos::os::kernel::UserProcessRunResult run_result =
            context.os_kernel().exec_user_file(
                context.process().id(),
                request.path,
                program_arguments,
                request.max_steps);
        std::optional<mnos::os::kernel::ProcessWaitResult> wait_result;
        if (shell_run_status_should_wait(run_result.status()))
        {
            wait_result = context.os_kernel().wait_process(
                context.process(),
                context.thread(),
                run_result.process_id());
        }
        shell_write(context, shell_run_result_text(request.path, run_result, wait_result));
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::invalid_argument& error)
    {
        shell_write_run_error(context, SHELL_RUN_INVALID_EXECUTABLE_PREFIX, error.what());
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::out_of_range& error)
    {
        shell_write_run_error(context, SHELL_RUN_INVALID_EXECUTABLE_PREFIX, error.what());
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::overflow_error& error)
    {
        shell_write_run_error(context, SHELL_RUN_INVALID_EXECUTABLE_PREFIX, error.what());
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::length_error& error)
    {
        shell_write_run_error(context, SHELL_RUN_INVALID_EXECUTABLE_PREFIX, error.what());
        return mnos::os::shell::ShellCommandResult::handled();
    }
    catch (const std::runtime_error& error)
    {
        shell_write_run_error(context, SHELL_RUN_INVALID_EXECUTABLE_PREFIX, error.what());
        return mnos::os::shell::ShellCommandResult::handled();
    }
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_exit(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext&)
{
    return mnos::os::shell::ShellCommandResult::exit_requested();
}
}

namespace mnos::os::shell
{
ShellCommand::ShellCommand(std::string name, std::vector<std::string> arguments) :
    name_(std::move(name)),
    arguments_(std::move(arguments))
{
}

std::string_view ShellCommand::name() const noexcept
{
    return this->name_;
}

const std::vector<std::string>& ShellCommand::arguments() const noexcept
{
    return this->arguments_;
}

std::size_t ShellCommand::argument_count() const noexcept
{
    return this->arguments_.size();
}

std::string_view ShellCommand::argument_at(const std::size_t index) const
{
    if (index >= this->arguments_.size())
    {
        throw std::out_of_range{SHELL_ARGUMENT_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return this->arguments_[index];
}

ShellParseResult ShellParseResult::command(ShellCommand command)
{
    return ShellParseResult{ShellParseStatus::COMMAND, std::move(command)};
}

ShellParseResult ShellParseResult::empty()
{
    return ShellParseResult{ShellParseStatus::EMPTY, std::nullopt};
}

ShellParseResult ShellParseResult::unterminated_quote()
{
    return ShellParseResult{ShellParseStatus::UNTERMINATED_QUOTE, std::nullopt};
}

ShellParseResult::ShellParseResult(
    const ShellParseStatus status,
    std::optional<ShellCommand> command) :
    status_(status),
    command_(std::move(command))
{
}

ShellParseStatus ShellParseResult::status() const noexcept
{
    return this->status_;
}

bool ShellParseResult::has_command() const noexcept
{
    return this->command_.has_value();
}

const ShellCommand& ShellParseResult::command() const
{
    if (!this->command_.has_value())
    {
        throw std::logic_error{SHELL_COMMAND_MISSING_MESSAGE};
    }
    return this->command_.value();
}

ShellParseResult ShellParser::parse(const std::string_view line) const
{
    std::vector<std::string> tokens;
    std::string token;
    bool token_started = false;
    bool escaped = false;
    bool quoted = false;
    char quote_character = '\0';

    for (const char character : line)
    {
        if (escaped)
        {
            token.push_back(character);
            token_started = true;
            escaped = false;
            continue;
        }

        if (character == '\\')
        {
            escaped = true;
            token_started = true;
            continue;
        }

        if (quoted)
        {
            if (character == quote_character)
            {
                quoted = false;
                continue;
            }
            token.push_back(character);
            token_started = true;
            continue;
        }

        if (shell_character_is_quote(character))
        {
            quoted = true;
            quote_character = character;
            token_started = true;
            continue;
        }

        if (shell_character_is_whitespace(character))
        {
            if (token_started)
            {
                tokens.push_back(std::move(token));
                token.clear();
                token_started = false;
            }
            continue;
        }

        token.push_back(character);
        token_started = true;
    }

    if (quoted)
    {
        return ShellParseResult::unterminated_quote();
    }
    if (escaped)
    {
        token.push_back('\\');
    }
    if (token_started)
    {
        tokens.push_back(std::move(token));
    }
    if (tokens.empty())
    {
        return ShellParseResult::empty();
    }

    std::string command_name = std::move(tokens.front());
    std::vector<std::string> arguments;
    arguments.reserve(tokens.size() - std::size_t{1});
    for (std::size_t token_index = std::size_t{1}; token_index < tokens.size(); ++token_index)
    {
        arguments.push_back(std::move(tokens[token_index]));
    }
    return ShellParseResult::command(ShellCommand{std::move(command_name), std::move(arguments)});
}

ShellCommandResult ShellCommandResult::handled() noexcept
{
    return ShellCommandResult{ShellCommandStatus::HANDLED};
}

ShellCommandResult ShellCommandResult::empty() noexcept
{
    return ShellCommandResult{ShellCommandStatus::EMPTY};
}

ShellCommandResult ShellCommandResult::unknown_command() noexcept
{
    return ShellCommandResult{ShellCommandStatus::UNKNOWN_COMMAND};
}

ShellCommandResult ShellCommandResult::parse_error() noexcept
{
    return ShellCommandResult{ShellCommandStatus::PARSE_ERROR};
}

ShellCommandResult ShellCommandResult::exit_requested() noexcept
{
    return ShellCommandResult{ShellCommandStatus::EXIT_REQUESTED};
}

ShellCommandResult::ShellCommandResult(const ShellCommandStatus status) noexcept : status_(status)
{
}

ShellCommandStatus ShellCommandResult::status() const noexcept
{
    return this->status_;
}

bool ShellCommandResult::is_handled() const noexcept
{
    return this->status_ == ShellCommandStatus::HANDLED;
}

ShellContext::ShellContext(kernel::Kernel& os_kernel) noexcept : os_kernel_(&os_kernel)
{
}

ShellContext::ShellContext(
    kernel::Kernel& os_kernel,
    proc::Process& process,
    sched::ThreadContext& thread) noexcept :
    os_kernel_(&os_kernel),
    process_(&process),
    thread_(&thread)
{
}

kernel::Kernel& ShellContext::os_kernel() noexcept
{
    return *this->os_kernel_;
}

const kernel::Kernel& ShellContext::os_kernel() const noexcept
{
    return *this->os_kernel_;
}

bool ShellContext::has_process_context() const noexcept
{
    return this->process_ != nullptr && this->thread_ != nullptr;
}

proc::Process& ShellContext::process()
{
    if (this->process_ == nullptr)
    {
        throw std::logic_error{SHELL_PROCESS_CONTEXT_MISSING_MESSAGE};
    }
    return *this->process_;
}

const proc::Process& ShellContext::process() const
{
    if (this->process_ == nullptr)
    {
        throw std::logic_error{SHELL_PROCESS_CONTEXT_MISSING_MESSAGE};
    }
    return *this->process_;
}

sched::ThreadContext& ShellContext::thread()
{
    if (this->thread_ == nullptr)
    {
        throw std::logic_error{SHELL_THREAD_CONTEXT_MISSING_MESSAGE};
    }
    return *this->thread_;
}

const sched::ThreadContext& ShellContext::thread() const
{
    if (this->thread_ == nullptr)
    {
        throw std::logic_error{SHELL_THREAD_CONTEXT_MISSING_MESSAGE};
    }
    return *this->thread_;
}

bool ShellBuiltinRegistry::contains(const std::string_view name) const noexcept
{
    return this->find(name).has_value();
}

std::optional<ShellBuiltinInfo> ShellBuiltinRegistry::find(const std::string_view name) const noexcept
{
    const ShellBuiltinSpec* const builtin = shell_find_builtin(name);
    if (builtin == nullptr)
    {
        return std::nullopt;
    }
    return builtin->info;
}

std::size_t ShellBuiltinRegistry::size() const noexcept
{
    return shell_builtin_catalog().size();
}

ShellBuiltinInfo ShellBuiltinRegistry::info_at(const std::size_t index) const
{
    const std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT>& catalog = shell_builtin_catalog();
    if (index >= catalog.size())
    {
        throw std::out_of_range{SHELL_BUILTIN_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return catalog[index].info;
}

std::string_view ShellBuiltinRegistry::name_at(const std::size_t index) const
{
    return this->info_at(index).name;
}

std::string_view ShellBuiltinRegistry::syntax_at(const std::size_t index) const
{
    return this->info_at(index).syntax;
}

std::string_view ShellBuiltinRegistry::description_at(const std::size_t index) const
{
    return this->info_at(index).description;
}

ShellCommandResult ShellBuiltinRegistry::execute(const ShellCommand& command, ShellContext& context) const
{
    const std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT>& catalog = shell_builtin_catalog();
    const auto builtin = std::ranges::find_if(
        catalog,
        [&command](const ShellBuiltinSpec& spec) noexcept
        {
            return spec.info.name == command.name();
        });
    if (builtin == catalog.end())
    {
        shell_write_unknown_command(context, command.name());
        return ShellCommandResult::unknown_command();
    }
    return builtin->handler(command, context);
}

Shell::Shell(kernel::Kernel& os_kernel) noexcept : context_(os_kernel)
{
}

Shell::Shell(kernel::Kernel& os_kernel, proc::Process& process, sched::ThreadContext& thread) noexcept :
    context_(os_kernel, process, thread)
{
}

bool Shell::running() const noexcept
{
    return this->running_;
}

ShellCommandResult Shell::execute_line(const std::string_view line)
{
    const ShellParseResult parse_result = this->parser_.parse(line);
    switch (parse_result.status())
    {
    case ShellParseStatus::EMPTY:
        return ShellCommandResult::empty();
    case ShellParseStatus::UNTERMINATED_QUOTE:
        shell_write(this->context_, SHELL_PARSE_ERROR_UNTERMINATED_QUOTE);
        return ShellCommandResult::parse_error();
    case ShellParseStatus::COMMAND:
    {
        const ShellCommandResult result = this->builtins_.execute(parse_result.command(), this->context_);
        if (result.status() == ShellCommandStatus::EXIT_REQUESTED)
        {
            this->running_ = false;
        }
        return result;
    }
    case ShellParseStatus::COUNT:
    default:
        shell_write(this->context_, SHELL_PARSE_ERROR_UNTERMINATED_QUOTE);
        return ShellCommandResult::parse_error();
    }
}
}
