#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process_context.hpp>
#include <mnos/os/proc/user_loader.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/shell/shell.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace io = mnos::os::io;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;
namespace shell = mnos::os::shell;

namespace
{
using ::testing::Eq;
using ::testing::HasSubstr;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{256});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr mm::VirtualAddress TEST_TEXT_BASE = mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mm::VirtualAddress TEST_DATA_BASE = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::VirtualAddress TEST_UNMAPPED_USER_BUFFER = mm::ADDRESS_LAYOUT_USER_HEAP_BASE + mm::MM_PAGE_SIZE_BYTES;
constexpr mm::AddressValue TEST_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr cpu::Byte TEST_TEXT_BYTE = cpu::Byte{0x90};
constexpr cpu::Byte TEST_DATA_BYTE = cpu::Byte{0};
constexpr std::string_view TEST_INPUT_LINE = "input\n";
constexpr std::string_view TEST_OUTPUT_LINE = "out\n";
constexpr std::string_view TEST_OUTPUT_VISIBLE_TEXT = "out";
constexpr std::size_t TEST_SMALL_BUFFER_SIZE = std::size_t{16};

[[nodiscard]] proc::UserProgram make_program()
{
    proc::UserProgram program{TEST_TEXT_BASE};
    program.set_initial_stack_size_bytes(TEST_STACK_SIZE_BYTES);
    program.add_segment(proc::UserSegment::text(TEST_TEXT_BASE, std::vector<cpu::Byte>{TEST_TEXT_BYTE}));
    program.add_segment(proc::UserSegment::data(TEST_DATA_BASE, std::vector<cpu::Byte>{TEST_DATA_BYTE}));
    return program;
}

void write_syscall_number(sched::ThreadContext& thread, const kernel::SyscallNumber number)
{
    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(number));
}

void write_syscall_arguments(
    sched::ThreadContext& thread,
    const cpu::Qword arg0,
    const cpu::Qword arg1,
    const cpu::Qword arg2)
{
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, arg0);
    thread.cpu_state().registers().write(cpu::RegisterId::RSI, arg1);
    thread.cpu_state().registers().write(cpu::RegisterId::RDX, arg2);
}

void initialize_user_like_thread(proc::Process& process, sched::ThreadContext& thread)
{
    thread.reset_cpu_state();
    thread.cpu_state().set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    thread.cpu_state().set_rip(TEST_TEXT_BASE.value());
    thread.cpu_state().registers().write(cpu::RegisterId::RSP, mm::ADDRESS_LAYOUT_USER_STACK_TOP.value());
    process.address_space().activate(
        thread.cpu_state(),
        proc::process_context_id_for(process.id()),
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
}

void write_user_bytes(
    platform::Machine& machine,
    proc::Process& process,
    const mm::VirtualAddress address,
    const std::string_view text)
{
    const cpu_memory::PageTranslation translation = process.address_space().page_translation(
        address,
        cpu_memory::MemoryAccessKind::WRITE,
        cpu_system::PrivilegeLevel::RING3);
    for (std::size_t byte_index = std::size_t{0}; byte_index < text.size(); ++byte_index)
    {
        machine.memory_bus().write(
            translation.translate(mm::to_cpu_address(address) + static_cast<cpu::Address64>(byte_index)),
            cpu::DataSize::BYTE,
            static_cast<cpu::Byte>(static_cast<unsigned char>(text[byte_index])));
    }
}

[[nodiscard]] std::string read_user_bytes(
    platform::Machine& machine,
    proc::Process& process,
    const mm::VirtualAddress address,
    const std::size_t byte_count)
{
    const cpu_memory::PageTranslation translation = process.address_space().page_translation(
        address,
        cpu_memory::MemoryAccessKind::READ,
        cpu_system::PrivilegeLevel::RING3);
    std::string output;
    output.reserve(byte_count);
    for (std::size_t byte_index = std::size_t{0}; byte_index < byte_count; ++byte_index)
    {
        const cpu::Qword byte_value = machine.memory_bus().read(
            translation.translate(mm::to_cpu_address(address) + static_cast<cpu::Address64>(byte_index)),
            cpu::DataSize::BYTE);
        output.push_back(static_cast<char>(byte_value));
    }
    return output;
}

[[nodiscard]] std::string display_text(const platform::Machine& machine)
{
    return machine.terminal_device().display().render_text();
}
}

TEST(Stage13ShellIoIntegrationTest, KernelFdBridgeConnectsStdioToTerminal)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    ASSERT_TRUE(os_kernel.has_stage13_services());

    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& reader = os_kernel.create_thread(process);
    sched::ThreadContext& worker = os_kernel.create_thread(process);
    ASSERT_EQ(os_kernel.scheduler().schedule_next(), &reader);

    EXPECT_THAT(
        os_kernel.write_fd(process, io::FileDescriptor::stdout(), "out").status(),
        Eq(io::IoStatus::READY));
    EXPECT_THAT(
        os_kernel.write_fd(process, io::FileDescriptor::stderr(), "err").byte_count(),
        Eq(std::size_t{3}));
    EXPECT_THAT(machine.terminal_device().display().line(std::size_t{0}).substr(std::size_t{0}, std::size_t{6}), Eq("outerr"));
    EXPECT_THAT(
        os_kernel.write_fd(process, io::FileDescriptor::stdin(), "bad").status(),
        Eq(io::IoStatus::BAD_DESCRIPTOR));
    std::array<char, TEST_SMALL_BUFFER_SIZE> buffer{};
    EXPECT_THAT(
        os_kernel.read_fd(process, reader, io::FileDescriptor::stdout(), buffer).status(),
        Eq(io::IoStatus::BAD_DESCRIPTOR));

    const io::IoResult blocked = os_kernel.read_fd(process, reader, io::FileDescriptor::stdin(), buffer);
    EXPECT_TRUE(blocked.is_blocked());
    EXPECT_THAT(reader.state(), Eq(sched::ThreadState::BLOCKED));
    ASSERT_TRUE(os_kernel.scheduler().has_current());
    EXPECT_EQ(&os_kernel.scheduler().current(), &worker);

    const std::vector<sched::ThreadContext*> readers = os_kernel.submit_terminal_input(TEST_INPUT_LINE);
    ASSERT_THAT(readers.size(), Eq(std::size_t{1}));
    EXPECT_EQ(readers.at(std::size_t{0}), &reader);
    EXPECT_THAT(reader.state(), Eq(sched::ThreadState::READY));
    ASSERT_EQ(os_kernel.scheduler().schedule_next(), &reader);
    const io::IoResult ready = os_kernel.read_fd(process, reader, io::FileDescriptor::stdin(), buffer);
    EXPECT_TRUE(ready.is_ready());
    EXPECT_THAT(ready.byte_count(), Eq(TEST_INPUT_LINE.size()));
    const std::string_view ready_text{buffer.data(), ready.byte_count()};
    EXPECT_THAT(ready_text, Eq(TEST_INPUT_LINE));
}

TEST(Stage13ShellIoIntegrationTest, ReadWriteSyscallsBridgeUserBuffersAndTty)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& process = os_kernel.create_user_process(make_program());
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});

    static_cast<void>(os_kernel.submit_terminal_input(TEST_INPUT_LINE));
    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdin().value()),
        TEST_DATA_BASE.value(),
        static_cast<cpu::Qword>(TEST_INPUT_LINE.size()));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_INPUT_LINE.size())));
    EXPECT_THAT(read_user_bytes(machine, process, TEST_DATA_BASE, TEST_INPUT_LINE.size()), Eq(std::string{TEST_INPUT_LINE}));

    write_user_bytes(machine, process, TEST_DATA_BASE, TEST_OUTPUT_LINE);
    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdout().value()),
        TEST_DATA_BASE.value(),
        static_cast<cpu::Qword>(TEST_OUTPUT_LINE.size()));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_OUTPUT_LINE.size())));
    EXPECT_THAT(
        machine.terminal_device().display().line(std::size_t{1}).substr(std::size_t{0}, TEST_OUTPUT_VISIBLE_TEXT.size()),
        Eq(std::string{TEST_OUTPUT_VISIBLE_TEXT}));
}

TEST(Stage13ShellIoIntegrationTest, ReadSyscallBlocksCurrentThreadWhenStdinIsEmpty)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& process = os_kernel.create_user_process(make_program());
    sched::ThreadContext& reader = process.thread_at(std::size_t{0});
    sched::ThreadContext& worker = os_kernel.create_thread(process);
    initialize_user_like_thread(process, worker);
    ASSERT_EQ(os_kernel.scheduler().schedule_next(), &reader);

    write_syscall_number(reader, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        reader,
        static_cast<cpu::Qword>(io::FileDescriptor::stdin().value()),
        TEST_DATA_BASE.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, reader), Eq(kernel::SyscallResult::BLOCKED));
    EXPECT_THAT(reader.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::SYSCALL_SUCCESS_RESULT));
    EXPECT_THAT(reader.state(), Eq(sched::ThreadState::BLOCKED));
    ASSERT_TRUE(os_kernel.scheduler().has_current());
    EXPECT_EQ(&os_kernel.scheduler().current(), &worker);
}

TEST(Stage13ShellIoIntegrationTest, IoSyscallsReportDescriptorAndBufferErrorsInOrder)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& process = os_kernel.create_user_process(make_program());
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});

    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        cpu::Qword{99},
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_DESCRIPTOR));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_FILE_DESCRIPTOR)));

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        thread,
        cpu::Qword{99},
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_DESCRIPTOR));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_FILE_DESCRIPTOR)));

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdin().value()),
        TEST_DATA_BASE.value(),
        cpu::Qword{0});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::SYSCALL_SUCCESS_RESULT));

    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdout().value()),
        TEST_DATA_BASE.value(),
        cpu::Qword{0});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::SYSCALL_SUCCESS_RESULT));

    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdout().value()),
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_ADDRESS)));

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdin().value()),
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_ADDRESS)));

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdin().value()),
        TEST_UNMAPPED_USER_BUFFER.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_ADDRESS)));

    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdout().value()),
        TEST_UNMAPPED_USER_BUFFER.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_ADDRESS)));

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdin().value()),
        TEST_DATA_BASE.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_IO_MAX_TRANSFER_BYTES + std::size_t{1}));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::INVALID_ARGUMENT)));

    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        static_cast<cpu::Qword>(io::FileDescriptor::stdout().value()),
        TEST_DATA_BASE.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_IO_MAX_TRANSFER_BYTES + std::size_t{1}));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::INVALID_ARGUMENT)));

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::OPERATION_NOT_SUPPORTED)));

    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::OPERATION_NOT_SUPPORTED)));
}

TEST(Stage13ShellIoIntegrationTest, ShellExecutesBuiltinsThroughKernelConsole)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    static_cast<void>(os_kernel.create_process());
    shell::Shell session{os_kernel};

    EXPECT_TRUE(session.running());
    EXPECT_THAT(session.execute_line("   ").status(), Eq(shell::ShellCommandStatus::EMPTY));

    EXPECT_THAT(session.execute_line("echo alpha \"two words\"").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(machine.terminal_device().display().line(std::size_t{0}), HasSubstr("alpha two words"));

    EXPECT_THAT(session.execute_line("help").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("builtins: help clear echo ps mem cpu ticks exit"));

    EXPECT_THAT(session.execute_line("clear").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_TRUE(machine.terminal_device().display().empty());

    EXPECT_THAT(session.execute_line("ps").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("pid threads states"));

    EXPECT_THAT(session.execute_line("mem").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("memory_pages total="));

    EXPECT_THAT(session.execute_line("cpu").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("cores=2"));

    EXPECT_THAT(session.execute_line("ticks").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("ticks="));

    EXPECT_THAT(session.execute_line("missing").status(), Eq(shell::ShellCommandStatus::UNKNOWN_COMMAND));
    EXPECT_THAT(display_text(machine), HasSubstr("unknown command: missing"));

    EXPECT_THAT(session.execute_line("echo \"unterminated").status(), Eq(shell::ShellCommandStatus::PARSE_ERROR));
    EXPECT_THAT(display_text(machine), HasSubstr("parse error: unterminated quote"));

    EXPECT_THAT(session.execute_line("exit").status(), Eq(shell::ShellCommandStatus::EXIT_REQUESTED));
    EXPECT_FALSE(session.running());
}
