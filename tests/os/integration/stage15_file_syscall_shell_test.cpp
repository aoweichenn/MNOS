#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
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

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{512});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr mm::VirtualAddress TEST_TEXT_BASE = mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mm::VirtualAddress TEST_DATA_BASE = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::VirtualAddress TEST_PATH_ADDRESS = TEST_DATA_BASE;
constexpr mm::VirtualAddress TEST_CONTENT_ADDRESS = TEST_DATA_BASE + mm::AddressValue{256};
constexpr mm::VirtualAddress TEST_READ_ADDRESS = TEST_DATA_BASE + mm::AddressValue{512};
constexpr mm::VirtualAddress TEST_STAT_ADDRESS = TEST_DATA_BASE + mm::AddressValue{768};
constexpr mm::VirtualAddress TEST_DIRENT_ADDRESS = TEST_DATA_BASE + mm::AddressValue{1024};
constexpr mm::VirtualAddress TEST_UNMAPPED_USER_ADDRESS = TEST_DATA_BASE + mm::MM_PAGE_SIZE_BYTES;
constexpr mm::AddressValue TEST_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr cpu::Byte TEST_TEXT_BYTE = cpu::Byte{0x90};
constexpr cpu::Byte TEST_DATA_BYTE = cpu::Byte{0};
constexpr char TEST_NO_SPACE_PAYLOAD_BYTE = 'x';
constexpr std::string_view TEST_FILE_PATH = "/notes";
constexpr std::string_view TEST_FILE_NAME = "notes";
constexpr std::string_view TEST_ROOT_PATH = "/";
constexpr std::string_view TEST_FILE_CONTENT = "hello stage15";
constexpr std::string_view TEST_MISSING_PATH = "/missing";
constexpr std::string_view TEST_WRITE_ONLY_PATH = "/writeonly";
constexpr std::string_view TEST_PLAIN_FILE_PATH = "/plain";
constexpr std::string_view TEST_OVERFLOW_PATH = "/overflow";
constexpr std::size_t TEST_READ_BUFFER_SIZE = std::size_t{32};
constexpr std::size_t TEST_ROOT_DIRENT_BUFFER_SIZE = kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES * std::size_t{4};
constexpr std::size_t TEST_NO_SPACE_WRITE_SIZE = kernel::SYSCALL_IO_MAX_TRANSFER_BYTES;
constexpr std::size_t TEST_INODE_EXHAUSTION_ATTEMPT_COUNT =
    static_cast<std::size_t>(kernel::KERNEL_STAGE15_ROOT_INODE_COUNT);
constexpr std::size_t TEST_SHARED_ONE_BYTE_READ = std::size_t{1};
constexpr std::size_t TEST_SHARED_TWO_BYTE_READ = std::size_t{2};
constexpr std::size_t TEST_STAT_KIND_OFFSET = std::size_t{0};
constexpr std::size_t TEST_STAT_SIZE_OFFSET = std::size_t{8};
constexpr std::size_t TEST_STAT_INODE_OFFSET = std::size_t{16};
constexpr std::size_t TEST_DIRENT_KIND_OFFSET = std::size_t{0};
constexpr std::size_t TEST_DIRENT_INODE_OFFSET = std::size_t{8};
constexpr std::size_t TEST_DIRENT_NAME_LENGTH_OFFSET = std::size_t{16};
constexpr std::size_t TEST_DIRENT_NAME_OFFSET = std::size_t{24};
constexpr std::size_t TEST_BITS_PER_BYTE = std::size_t{8};

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
    const cpu::Qword arg1 = cpu::Qword{0},
    const cpu::Qword arg2 = cpu::Qword{0},
    const cpu::Qword arg3 = cpu::Qword{0},
    const cpu::Qword arg4 = cpu::Qword{0},
    const cpu::Qword arg5 = cpu::Qword{0})
{
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, arg0);
    thread.cpu_state().registers().write(cpu::RegisterId::RSI, arg1);
    thread.cpu_state().registers().write(cpu::RegisterId::RDX, arg2);
    thread.cpu_state().registers().write(cpu::RegisterId::R10, arg3);
    thread.cpu_state().registers().write(cpu::RegisterId::R8, arg4);
    thread.cpu_state().registers().write(cpu::RegisterId::R9, arg5);
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

[[nodiscard]] std::uint64_t read_u64_le(const std::string_view bytes, const std::size_t offset) noexcept
{
    std::uint64_t value = std::uint64_t{0};
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + byte_index])) <<
                 static_cast<unsigned>(byte_index * TEST_BITS_PER_BYTE);
    }
    return value;
}

[[nodiscard]] std::string dirent_name(const std::string_view record)
{
    const std::size_t name_size = static_cast<std::size_t>(read_u64_le(record, TEST_DIRENT_NAME_LENGTH_OFFSET));
    return std::string{record.substr(TEST_DIRENT_NAME_OFFSET, name_size)};
}

[[nodiscard]] std::string display_text(const platform::Machine& machine)
{
    return machine.terminal_device().display().render_text();
}

void expect_syscall_error(
    sched::ThreadContext& thread,
    const kernel::SyscallResult actual_result,
    const kernel::SyscallResult expected_result,
    const kernel::SyscallError expected_error)
{
    EXPECT_THAT(actual_result, Eq(expected_result));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(expected_error)));
}
}

TEST(Stage15FileSyscallShellIntegrationTest, FileSyscallsReadWriteStatAndReaddirThroughVfsFd)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    ASSERT_TRUE(os_kernel.has_stage15_services());

    proc::Process& process = os_kernel.create_user_process(make_program());
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});
    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_FILE_PATH);
    write_user_bytes(machine, process, TEST_CONTENT_ADDRESS, TEST_FILE_CONTENT);

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_FILE_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_READ | kernel::SYSCALL_OPEN_FLAG_WRITE | kernel::SYSCALL_OPEN_FLAG_CREATE);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    const cpu::Qword writable_fd = thread.cpu_state().registers().read(cpu::RegisterId::RAX);
    EXPECT_THAT(writable_fd, Eq(cpu::Qword{3}));

    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        writable_fd,
        TEST_CONTENT_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_FILE_CONTENT.size()));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_FILE_CONTENT.size())));

    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_FILE_PATH.size()),
        TEST_STAT_ADDRESS.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    const std::string stat_record = read_user_bytes(machine, process, TEST_STAT_ADDRESS, kernel::SYSCALL_STAT_RECORD_SIZE_BYTES);
    EXPECT_THAT(read_u64_le(stat_record, TEST_STAT_KIND_OFFSET), Eq(kernel::SYSCALL_FILE_KIND_FILE));
    EXPECT_THAT(read_u64_le(stat_record, TEST_STAT_SIZE_OFFSET), Eq(static_cast<std::uint64_t>(TEST_FILE_CONTENT.size())));
    EXPECT_NE(read_u64_le(stat_record, TEST_STAT_INODE_OFFSET), std::uint64_t{0});

    write_syscall_number(thread, kernel::SyscallNumber::CLOSE);
    write_syscall_arguments(thread, writable_fd);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_FILE_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_READ);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    const cpu::Qword readable_fd = thread.cpu_state().registers().read(cpu::RegisterId::RAX);

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(thread, readable_fd, TEST_READ_ADDRESS.value(), static_cast<cpu::Qword>(TEST_READ_BUFFER_SIZE));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_FILE_CONTENT.size())));
    EXPECT_THAT(read_user_bytes(machine, process, TEST_READ_ADDRESS, TEST_FILE_CONTENT.size()), Eq(std::string{TEST_FILE_CONTENT}));

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(thread, readable_fd, TEST_READ_ADDRESS.value(), static_cast<cpu::Qword>(TEST_READ_BUFFER_SIZE));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_ROOT_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_PATH.size()),
        TEST_DIRENT_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_DIRENT_BUFFER_SIZE));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES)));
    const std::string dirent_record =
        read_user_bytes(machine, process, TEST_DIRENT_ADDRESS, kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES);
    EXPECT_THAT(read_u64_le(dirent_record, TEST_DIRENT_KIND_OFFSET), Eq(kernel::SYSCALL_FILE_KIND_FILE));
    EXPECT_NE(read_u64_le(dirent_record, TEST_DIRENT_INODE_OFFSET), std::uint64_t{0});
    EXPECT_THAT(dirent_name(dirent_record), Eq(std::string{TEST_FILE_NAME}));
}

TEST(Stage15FileSyscallShellIntegrationTest, FileSyscallsRejectInvalidInputsAndPreserveIoErrorOrder)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& process = os_kernel.create_user_process(make_program());
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});
    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_MISSING_PATH);

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_MISSING_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_READ);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::NOT_FOUND));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::syscall_error_result(kernel::SyscallError::NO_ENTRY)));

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_MISSING_PATH.size()),
        cpu::Qword{0});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_PATH_MAX_BYTES + std::size_t{1}),
        kernel::SYSCALL_OPEN_FLAG_READ);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    write_syscall_number(thread, kernel::SyscallNumber::CLOSE);
    write_syscall_arguments(thread, cpu::Qword{99});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_DESCRIPTOR));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_FILE_DESCRIPTOR)));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_WRITE_ONLY_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_WRITE_ONLY_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_WRITE | kernel::SYSCALL_OPEN_FLAG_CREATE);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    const cpu::Qword write_only_fd = thread.cpu_state().registers().read(cpu::RegisterId::RAX);

    write_syscall_number(thread, kernel::SyscallNumber::READ);
    write_syscall_arguments(
        thread,
        write_only_fd,
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_DESCRIPTOR));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_FILE_DESCRIPTOR)));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_PLAIN_FILE_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_PLAIN_FILE_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_READ | kernel::SYSCALL_OPEN_FLAG_WRITE | kernel::SYSCALL_OPEN_FLAG_CREATE);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));

    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_PLAIN_FILE_PATH.size()),
        TEST_DIRENT_ADDRESS.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::NOT_DIRECTORY)));
}

TEST(Stage15FileSyscallShellIntegrationTest, FileSyscallsValidateContextPathsAndOutputBuffers)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& process = os_kernel.create_user_process(make_program());
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    write_syscall_number(thread, kernel::SyscallNumber::CLOSE);
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_ROOT_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_READ);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::IS_DIRECTORY)));

    std::string trailing_nul_path{"/nul"};
    trailing_nul_path.push_back('\0');
    write_user_bytes(machine, process, TEST_PATH_ADDRESS, trailing_nul_path);
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(trailing_nul_path.size()),
        kernel::SYSCALL_OPEN_FLAG_READ | kernel::SYSCALL_OPEN_FLAG_WRITE | kernel::SYSCALL_OPEN_FLAG_CREATE);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_TRUE(os_kernel.vfs().lookup("/nul").has_value());

    std::string embedded_nul_path{"/bad"};
    embedded_nul_path.push_back('\0');
    embedded_nul_path.push_back('x');
    write_user_bytes(machine, process, TEST_PATH_ADDRESS, embedded_nul_path);
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(embedded_nul_path.size()),
        kernel::SYSCALL_OPEN_FLAG_READ);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        cpu::Qword{1},
        kernel::SYSCALL_OPEN_FLAG_READ);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, "relative");
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        cpu::Qword{8},
        kernel::SYSCALL_OPEN_FLAG_READ | kernel::SYSCALL_OPEN_FLAG_CREATE);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_MISSING_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_MISSING_PATH.size()),
        TEST_STAT_ADDRESS.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::NOT_FOUND));

    static_cast<void>(os_kernel.vfs().create_file(TEST_PLAIN_FILE_PATH));
    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_PLAIN_FILE_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_PLAIN_FILE_PATH.size()),
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, "relative");
    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        cpu::Qword{8},
        TEST_STAT_ADDRESS.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_ROOT_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_PATH.size()),
        TEST_STAT_ADDRESS.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    const std::string root_stat =
        read_user_bytes(machine, process, TEST_STAT_ADDRESS, kernel::SYSCALL_STAT_RECORD_SIZE_BYTES);
    EXPECT_THAT(read_u64_le(root_stat, TEST_STAT_KIND_OFFSET), Eq(kernel::SYSCALL_FILE_KIND_DIRECTORY));

    static_cast<void>(os_kernel.vfs().create_directory("/direntdir"));
    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_PATH.size()),
        TEST_DIRENT_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_DIRENT_BUFFER_SIZE));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    const std::size_t dirent_bytes =
        static_cast<std::size_t>(thread.cpu_state().registers().read(cpu::RegisterId::RAX));
    const std::string dirent_records = read_user_bytes(machine, process, TEST_DIRENT_ADDRESS, dirent_bytes);
    bool found_directory_entry = false;
    for (std::size_t offset = std::size_t{0}; offset < dirent_records.size();
         offset += kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES)
    {
        const std::string_view record{dirent_records.data() + offset, kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES};
        found_directory_entry = found_directory_entry ||
            (read_u64_le(record, TEST_DIRENT_KIND_OFFSET) == kernel::SYSCALL_FILE_KIND_DIRECTORY &&
             dirent_name(record) == "direntdir");
    }
    EXPECT_TRUE(found_directory_entry);

    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_PLAIN_FILE_PATH.size()),
        TEST_DIRENT_ADDRESS.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_IO_MAX_TRANSFER_BYTES + std::size_t{1}));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, "relative");
    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        cpu::Qword{8},
        TEST_DIRENT_ADDRESS.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_MISSING_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_MISSING_PATH.size()),
        TEST_DIRENT_ADDRESS.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::NOT_FOUND));

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_ROOT_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_PATH.size()),
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        cpu::Qword{0});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));

    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_PATH.size()),
        mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES));
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    for (std::size_t file_index = std::size_t{0}; file_index < TEST_INODE_EXHAUSTION_ATTEMPT_COUNT; ++file_index)
    {
        try
        {
            static_cast<void>(os_kernel.vfs().create_file("/fill" + std::to_string(file_index)));
        }
        catch (const std::length_error&)
        {
            break;
        }
    }
    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_OVERFLOW_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_OVERFLOW_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_READ | kernel::SYSCALL_OPEN_FLAG_WRITE | kernel::SYSCALL_OPEN_FLAG_CREATE);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::NO_SPACE));
}

TEST(Stage15FileSyscallShellIntegrationTest, FileSyscallsRejectUnmappedUserBuffersAndReportWriteNoSpace)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& process = os_kernel.create_user_process(make_program());
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_FILE_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_FILE_PATH.size()),
        kernel::SYSCALL_OPEN_FLAG_READ | kernel::SYSCALL_OPEN_FLAG_WRITE | kernel::SYSCALL_OPEN_FLAG_CREATE);
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::HANDLED));
    const cpu::Qword writable_fd = thread.cpu_state().registers().read(cpu::RegisterId::RAX);

    write_syscall_number(thread, kernel::SyscallNumber::OPEN);
    write_syscall_arguments(
        thread,
        TEST_UNMAPPED_USER_ADDRESS.value(),
        cpu::Qword{1},
        kernel::SYSCALL_OPEN_FLAG_READ);
    expect_syscall_error(
        thread,
        os_kernel.dispatch_syscall(process, thread),
        kernel::SyscallResult::BAD_ADDRESS,
        kernel::SyscallError::BAD_ADDRESS);

    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    write_syscall_arguments(
        thread,
        TEST_UNMAPPED_USER_ADDRESS.value(),
        cpu::Qword{1},
        TEST_STAT_ADDRESS.value());
    expect_syscall_error(
        thread,
        os_kernel.dispatch_syscall(process, thread),
        kernel::SyscallResult::BAD_ADDRESS,
        kernel::SyscallError::BAD_ADDRESS);

    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_UNMAPPED_USER_ADDRESS.value(),
        cpu::Qword{1},
        TEST_DIRENT_ADDRESS.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES));
    expect_syscall_error(
        thread,
        os_kernel.dispatch_syscall(process, thread),
        kernel::SyscallResult::BAD_ADDRESS,
        kernel::SyscallError::BAD_ADDRESS);

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_FILE_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::STAT);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_FILE_PATH.size()),
        TEST_UNMAPPED_USER_ADDRESS.value());
    expect_syscall_error(
        thread,
        os_kernel.dispatch_syscall(process, thread),
        kernel::SyscallResult::BAD_ADDRESS,
        kernel::SyscallError::BAD_ADDRESS);

    write_user_bytes(machine, process, TEST_PATH_ADDRESS, TEST_ROOT_PATH);
    write_syscall_number(thread, kernel::SyscallNumber::READDIR);
    write_syscall_arguments(
        thread,
        TEST_PATH_ADDRESS.value(),
        static_cast<cpu::Qword>(TEST_ROOT_PATH.size()),
        TEST_UNMAPPED_USER_ADDRESS.value(),
        static_cast<cpu::Qword>(kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES));
    expect_syscall_error(
        thread,
        os_kernel.dispatch_syscall(process, thread),
        kernel::SyscallResult::BAD_ADDRESS,
        kernel::SyscallError::BAD_ADDRESS);

    const std::string too_large_payload(TEST_NO_SPACE_WRITE_SIZE, TEST_NO_SPACE_PAYLOAD_BYTE);
    write_user_bytes(machine, process, TEST_DATA_BASE, too_large_payload);
    write_syscall_number(thread, kernel::SyscallNumber::WRITE);
    write_syscall_arguments(
        thread,
        writable_fd,
        TEST_DATA_BASE.value(),
        static_cast<cpu::Qword>(too_large_payload.size()));
    expect_syscall_error(
        thread,
        os_kernel.dispatch_syscall(process, thread),
        kernel::SyscallResult::NO_SPACE,
        kernel::SyscallError::NO_SPACE);
}

TEST(Stage15FileSyscallShellIntegrationTest, ForkedProcessesShareOpenFileDescriptionOffsets)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& parent = os_kernel.create_process();
    sched::ThreadContext& parent_thread = os_kernel.create_thread(parent);
    const io::FileDescriptor write_fd =
        os_kernel.open_file(parent, "/shared", io::FileAccessMode::READ_WRITE, true);
    EXPECT_TRUE(os_kernel.write_fd(parent, write_fd, "abc").is_ready());
    EXPECT_TRUE(os_kernel.close_fd(parent, write_fd));

    const io::FileDescriptor read_fd =
        os_kernel.open_file(parent, "/shared", io::FileAccessMode::READ_ONLY, false);
    const std::array<mm::VirtualAddress, 0> cow_pages{};
    proc::Process& child = os_kernel.fork_process_cow(parent, cow_pages);
    sched::ThreadContext& child_thread = os_kernel.create_thread(child);

    std::array<char, TEST_SHARED_ONE_BYTE_READ> parent_buffer{};
    const io::IoResult parent_read = os_kernel.read_fd(parent, parent_thread, read_fd, parent_buffer);
    EXPECT_TRUE(parent_read.is_ready());
    EXPECT_THAT(parent_read.byte_count(), Eq(TEST_SHARED_ONE_BYTE_READ));
    EXPECT_THAT(parent_buffer[std::size_t{0}], Eq('a'));

    std::array<char, TEST_SHARED_TWO_BYTE_READ> child_buffer{};
    const io::IoResult child_read = os_kernel.read_fd(child, child_thread, read_fd, child_buffer);
    EXPECT_TRUE(child_read.is_ready());
    EXPECT_THAT(child_read.byte_count(), Eq(TEST_SHARED_TWO_BYTE_READ));
    const std::string_view child_text{child_buffer.data(), child_read.byte_count()};
    EXPECT_THAT(child_text, Eq(std::string_view{"bc"}));
}

TEST(Stage15FileSyscallShellIntegrationTest, ShellFileCommandsUseKernelVfs)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    shell::Shell session{os_kernel};

    EXPECT_THAT(session.execute_line("touch /notes").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(session.execute_line("write /notes hello fs").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(session.execute_line("cat /notes").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("hello fs"));

    EXPECT_THAT(session.execute_line("stat /notes").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("path=/notes kind=file"));
    EXPECT_THAT(display_text(machine), HasSubstr("size=9"));

    EXPECT_THAT(session.execute_line("ls /").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("notes"));
}

TEST(Stage15FileSyscallShellIntegrationTest, ShellFileCommandsReportUsageAndPathErrors)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    shell::Shell session{os_kernel};

    static_cast<void>(os_kernel.vfs().create_directory("/dir"));
    EXPECT_THAT(session.execute_line("ls / extra").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("usage: ls [path]"));
    EXPECT_THAT(session.execute_line("ls /missing").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("not found: /missing"));
    EXPECT_THAT(session.execute_line("touch /plain").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(session.execute_line("ls /plain").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("not a directory: /plain"));
    EXPECT_THAT(session.execute_line("ls relative").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("invalid path: relative"));
    EXPECT_THAT(session.execute_line("ls /").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("dir/"));

    EXPECT_THAT(session.execute_line("cat").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("usage: cat path"));
    EXPECT_THAT(session.execute_line("cat /missing").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("not found: /missing"));
    EXPECT_THAT(session.execute_line("cat /").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("is a directory: /"));
    EXPECT_THAT(session.execute_line("cat relative").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("invalid path: relative"));

    EXPECT_THAT(session.execute_line("touch").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("usage: touch path"));
    EXPECT_THAT(session.execute_line("touch /").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("is a directory: /"));
    EXPECT_THAT(session.execute_line("touch /missing/leaf").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("not found: /missing/leaf"));
    EXPECT_THAT(session.execute_line("touch relative").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("invalid path: relative"));

    EXPECT_THAT(session.execute_line("write /").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("usage: write path text..."));
    EXPECT_THAT(session.execute_line("write / hello").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("is a directory: /"));
    EXPECT_THAT(session.execute_line("write /missing/leaf hi").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("not found: /missing/leaf"));
    EXPECT_THAT(session.execute_line("write relative hi").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("invalid path: relative"));
    EXPECT_THAT(session.execute_line("write /auto hi").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_TRUE(os_kernel.vfs().lookup("/auto").has_value());

    EXPECT_THAT(session.execute_line("stat").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("usage: stat path"));
    EXPECT_THAT(session.execute_line("stat /missing").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("not found: /missing"));
    EXPECT_THAT(session.execute_line("stat relative").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("invalid path: relative"));
    EXPECT_THAT(session.execute_line("stat /").status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_THAT(display_text(machine), HasSubstr("path=/ kind=directory"));
}
