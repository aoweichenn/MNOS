#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/trap_controller.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/proc/process_context.hpp>

namespace
{
static_assert(
    mnos::os::kernel::SYSCALL_DIRENT_NAME_BYTES == mnos::os::fs::SIMPLE_FS_MAX_NAME_LENGTH,
    "syscall dirent ABI must match the VFS name capacity");

constexpr const char* KERNEL_STAGE11_NOT_READY_MESSAGE = "kernel stage11 services are not initialized";
constexpr std::size_t SYSCALL_STAT_KIND_OFFSET = std::size_t{0};
constexpr std::size_t SYSCALL_STAT_SIZE_OFFSET = std::size_t{8};
constexpr std::size_t SYSCALL_STAT_INODE_OFFSET = std::size_t{16};
constexpr std::size_t SYSCALL_DIRENT_KIND_OFFSET = std::size_t{0};
constexpr std::size_t SYSCALL_DIRENT_INODE_OFFSET = std::size_t{8};
constexpr std::size_t SYSCALL_DIRENT_NAME_LENGTH_OFFSET = std::size_t{16};
constexpr std::size_t SYSCALL_DIRENT_NAME_OFFSET = std::size_t{24};
constexpr std::size_t SYSCALL_BITS_PER_BYTE = std::size_t{8};
constexpr char SYSCALL_PATH_NUL = '\0';

[[nodiscard]] bool is_pending_user_syscall(const mnos::cpu::CpuState& cpu_state) noexcept
{
    return cpu_state.has_pending_trap() &&
        cpu_state.pending_trap().kind() == mnos::cpu::system::TrapKind::SYSCALL &&
        cpu_state.pending_trap().privilege_level() == mnos::cpu::system::PrivilegeLevel::RING3;
}

[[nodiscard]] bool is_pending_user_page_fault(const mnos::cpu::CpuState& cpu_state) noexcept
{
    return cpu_state.has_pending_trap() &&
        cpu_state.pending_trap().vector() == mnos::cpu::system::InterruptVector::page_fault() &&
        cpu_state.pending_trap().privilege_level() == mnos::cpu::system::PrivilegeLevel::RING3;
}

[[nodiscard]] bool is_valid_user_page(const mnos::os::mm::VirtualAddress virtual_page) noexcept
{
    return mnos::os::mm::is_page_aligned(virtual_page) &&
        mnos::os::mm::is_user_range(virtual_page, mnos::os::mm::MM_PAGE_SIZE_BYTES);
}

[[nodiscard]] bool is_valid_user_futex_word(const mnos::os::mm::VirtualAddress address) noexcept
{
    return mnos::os::mm::is_user_range(
        address,
        static_cast<mnos::os::mm::AddressValue>(mnos::cpu::DATA_SIZE_DWORD_BYTES));
}

[[nodiscard]] bool user_page_is_mapped(
    mnos::os::proc::Process& process,
    const mnos::os::mm::VirtualAddress virtual_page)
{
    try
    {
        static_cast<void>(process.address_space().page_translation(
            virtual_page,
            mnos::cpu::memory::MemoryAccessKind::READ,
            mnos::cpu::system::PrivilegeLevel::RING3));
        return true;
    }
    catch (const mnos::cpu::memory::PageFault&)
    {
        return false;
    }
}

[[nodiscard]] mnos::os::mm::VirtualAddress syscall_virtual_address(const mnos::cpu::Qword raw_address) noexcept
{
    return mnos::os::mm::VirtualAddress{static_cast<mnos::os::mm::AddressValue>(raw_address)};
}

[[nodiscard]] bool syscall_transfer_size_is_valid(const mnos::cpu::Qword raw_byte_count) noexcept
{
    return raw_byte_count <= static_cast<mnos::cpu::Qword>(mnos::os::kernel::SYSCALL_IO_MAX_TRANSFER_BYTES) &&
        raw_byte_count <= static_cast<mnos::cpu::Qword>(std::numeric_limits<std::size_t>::max());
}

[[nodiscard]] std::size_t syscall_transfer_size(const mnos::cpu::Qword raw_byte_count) noexcept
{
    return static_cast<std::size_t>(raw_byte_count);
}

[[nodiscard]] bool syscall_path_size_is_valid(const mnos::cpu::Qword raw_byte_count) noexcept
{
    return raw_byte_count > mnos::cpu::Qword{0} &&
        raw_byte_count <= static_cast<mnos::cpu::Qword>(mnos::os::kernel::SYSCALL_PATH_MAX_BYTES) &&
        raw_byte_count <= static_cast<mnos::cpu::Qword>(std::numeric_limits<std::size_t>::max());
}

[[nodiscard]] std::size_t syscall_path_size(const mnos::cpu::Qword raw_byte_count) noexcept
{
    return static_cast<std::size_t>(raw_byte_count);
}

[[nodiscard]] mnos::os::io::FileDescriptor syscall_file_descriptor(const mnos::cpu::Qword raw_descriptor) noexcept
{
    return mnos::os::io::file_descriptor_from_raw(static_cast<std::uint64_t>(raw_descriptor));
}

[[nodiscard]] bool syscall_open_flags_are_valid(const mnos::cpu::Qword raw_flags) noexcept
{
    const std::uint64_t flags = static_cast<std::uint64_t>(raw_flags);
    const bool has_unknown_flags = (flags & ~mnos::os::kernel::SYSCALL_OPEN_FLAG_VALID_MASK) != std::uint64_t{0};
    const bool requests_read = (flags & mnos::os::kernel::SYSCALL_OPEN_FLAG_READ) != std::uint64_t{0};
    const bool requests_write = (flags & mnos::os::kernel::SYSCALL_OPEN_FLAG_WRITE) != std::uint64_t{0};
    return !has_unknown_flags && (requests_read || requests_write);
}

[[nodiscard]] mnos::os::io::FileAccessMode syscall_open_access_mode(
    const mnos::cpu::Qword raw_flags) noexcept
{
    const std::uint64_t flags = static_cast<std::uint64_t>(raw_flags);
    const bool requests_read = (flags & mnos::os::kernel::SYSCALL_OPEN_FLAG_READ) != std::uint64_t{0};
    const bool requests_write = (flags & mnos::os::kernel::SYSCALL_OPEN_FLAG_WRITE) != std::uint64_t{0};
    if (requests_read && requests_write)
    {
        return mnos::os::io::FileAccessMode::READ_WRITE;
    }
    if (requests_read)
    {
        return mnos::os::io::FileAccessMode::READ_ONLY;
    }
    if (requests_write)
    {
        return mnos::os::io::FileAccessMode::WRITE_ONLY;
    }
    return mnos::os::io::FileAccessMode::COUNT;
}

[[nodiscard]] bool syscall_open_create_requested(const mnos::cpu::Qword raw_flags) noexcept
{
    const std::uint64_t flags = static_cast<std::uint64_t>(raw_flags);
    return (flags & mnos::os::kernel::SYSCALL_OPEN_FLAG_CREATE) != std::uint64_t{0};
}

[[nodiscard]] std::uint64_t syscall_file_kind_value(const mnos::os::fs::SimpleFsNodeKind kind) noexcept
{
    switch (kind)
    {
    case mnos::os::fs::SimpleFsNodeKind::FILE:
        return mnos::os::kernel::SYSCALL_FILE_KIND_FILE;
    case mnos::os::fs::SimpleFsNodeKind::DIRECTORY:
        return mnos::os::kernel::SYSCALL_FILE_KIND_DIRECTORY;
    case mnos::os::fs::SimpleFsNodeKind::COUNT:
        break;
    }
    return std::uint64_t{0};
}

[[nodiscard]] bool is_valid_user_io_range(
    const mnos::os::mm::VirtualAddress address,
    const std::size_t byte_count) noexcept
{
    return byte_count == std::size_t{0} ||
        mnos::os::mm::is_user_range(address, static_cast<mnos::os::mm::AddressValue>(byte_count));
}

void check_user_io_access(
    mnos::os::proc::Process& process,
    mnos::os::sched::ThreadContext& thread,
    const mnos::os::mm::VirtualAddress address,
    const std::size_t byte_count,
    const mnos::cpu::memory::MemoryAccessKind access_kind)
{
    mnos::cpu::memory::MemoryManagementUnit mmu;
    mmu.check_access_range(
        process.address_space().memory_bus(),
        thread.cpu_state().paging(),
        mnos::cpu::system::PrivilegeLevel::RING3,
        mnos::os::mm::to_cpu_address(address),
        byte_count,
        access_kind);
}

[[nodiscard]] std::vector<char> read_user_bytes(
    mnos::os::proc::Process& process,
    mnos::os::sched::ThreadContext& thread,
    const mnos::os::mm::VirtualAddress address,
    const std::size_t byte_count)
{
    std::vector<char> bytes(byte_count);
    mnos::cpu::memory::MemoryManagementUnit mmu;
    for (std::size_t byte_index = std::size_t{0}; byte_index < byte_count; ++byte_index)
    {
        const mnos::cpu::Qword byte_value = mmu.read(
            process.address_space().memory_bus(),
            thread.cpu_state().paging(),
            mnos::cpu::system::PrivilegeLevel::RING3,
            mnos::os::mm::to_cpu_address(address) + static_cast<mnos::cpu::Address64>(byte_index),
            mnos::cpu::DataSize::BYTE);
        bytes[byte_index] = static_cast<char>(byte_value);
    }
    return bytes;
}

void write_user_bytes(
    mnos::os::proc::Process& process,
    mnos::os::sched::ThreadContext& thread,
    const mnos::os::mm::VirtualAddress address,
    std::span<const char> bytes)
{
    mnos::cpu::memory::MemoryManagementUnit mmu;
    for (std::size_t byte_index = std::size_t{0}; byte_index < bytes.size(); ++byte_index)
    {
        const auto byte_value = static_cast<mnos::cpu::Byte>(static_cast<unsigned char>(bytes[byte_index]));
        mmu.write(
            process.address_space().memory_bus(),
            thread.cpu_state().paging(),
            mnos::cpu::system::PrivilegeLevel::RING3,
            mnos::os::mm::to_cpu_address(address) + static_cast<mnos::cpu::Address64>(byte_index),
            mnos::cpu::DataSize::BYTE,
            byte_value);
    }
}

void write_u64_le(std::span<char> bytes, const std::size_t offset, const std::uint64_t value) noexcept
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes[offset + byte_index] = static_cast<char>(
            static_cast<unsigned char>(
                value >> static_cast<unsigned>(byte_index * SYSCALL_BITS_PER_BYTE)));
    }
}

[[nodiscard]] std::array<char, mnos::os::kernel::SYSCALL_STAT_RECORD_SIZE_BYTES> serialize_stat_record(
    const mnos::os::fs::VfsNode& node) noexcept
{
    std::array<char, mnos::os::kernel::SYSCALL_STAT_RECORD_SIZE_BYTES> record{};
    write_u64_le(record, SYSCALL_STAT_KIND_OFFSET, syscall_file_kind_value(node.kind()));
    write_u64_le(record, SYSCALL_STAT_SIZE_OFFSET, node.size_bytes());
    write_u64_le(record, SYSCALL_STAT_INODE_OFFSET, node.inode().value());
    return record;
}

[[nodiscard]] std::array<char, mnos::os::kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES> serialize_dirent_record(
    const mnos::os::fs::SimpleFsDirectoryEntry& entry) noexcept
{
    std::array<char, mnos::os::kernel::SYSCALL_DIRENT_RECORD_SIZE_BYTES> record{};
    write_u64_le(record, SYSCALL_DIRENT_KIND_OFFSET, syscall_file_kind_value(entry.kind()));
    write_u64_le(record, SYSCALL_DIRENT_INODE_OFFSET, entry.inode().value());
    write_u64_le(record, SYSCALL_DIRENT_NAME_LENGTH_OFFSET, static_cast<std::uint64_t>(entry.name().size()));
    for (std::size_t char_index = std::size_t{0}; char_index < entry.name().size(); ++char_index)
    {
        record[SYSCALL_DIRENT_NAME_OFFSET + char_index] = entry.name()[char_index];
    }
    return record;
}

void finish_syscall_success(mnos::os::kernel::SyscallFrame& frame, const mnos::cpu::Qword value) noexcept
{
    frame.set_result(value);
}

[[nodiscard]] mnos::os::kernel::SyscallResult finish_syscall_error(
    mnos::os::kernel::SyscallFrame& frame,
    const mnos::os::kernel::SyscallError error,
    const mnos::os::kernel::SyscallResult result) noexcept
{
    frame.set_error(error);
    return result;
}

[[nodiscard]] mnos::os::kernel::SyscallResult read_syscall_path(
    mnos::os::proc::Process& process,
    mnos::os::sched::ThreadContext& thread,
    mnos::os::kernel::SyscallFrame& frame,
    const mnos::os::mm::VirtualAddress path_address,
    const std::size_t path_byte_count,
    std::string& path)
{
    if (!is_valid_user_io_range(path_address, path_byte_count))
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_ADDRESS,
            mnos::os::kernel::SyscallResult::BAD_ADDRESS);
    }

    try
    {
        check_user_io_access(
            process,
            thread,
            path_address,
            path_byte_count,
            mnos::cpu::memory::MemoryAccessKind::READ);
        std::vector<char> bytes = read_user_bytes(process, thread, path_address, path_byte_count);
        if (!bytes.empty() && bytes.back() == SYSCALL_PATH_NUL)
        {
            bytes.pop_back();
        }
        if (bytes.empty() || std::find(bytes.begin(), bytes.end(), SYSCALL_PATH_NUL) != bytes.end())
        {
            return finish_syscall_error(
                frame,
                mnos::os::kernel::SyscallError::INVALID_ARGUMENT,
                mnos::os::kernel::SyscallResult::INVALID_ARGUMENT);
        }
        path.assign(bytes.begin(), bytes.end());
        return mnos::os::kernel::SyscallResult::HANDLED;
    }
    catch (const std::bad_alloc&)
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::NO_MEMORY,
            mnos::os::kernel::SyscallResult::OUT_OF_MEMORY);
    }
    catch (const mnos::cpu::memory::PageFault&)
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_ADDRESS,
            mnos::os::kernel::SyscallResult::BAD_ADDRESS);
    }
    catch (const std::out_of_range&)
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_ADDRESS,
            mnos::os::kernel::SyscallResult::BAD_ADDRESS);
    }
}

[[nodiscard]] mnos::os::kernel::SyscallResult read_syscall_path_argument(
    mnos::os::proc::Process& process,
    mnos::os::sched::ThreadContext& thread,
    mnos::os::kernel::SyscallFrame& frame,
    const mnos::os::kernel::SyscallArgument address_argument,
    const mnos::os::kernel::SyscallArgument size_argument,
    std::string& path)
{
    const mnos::cpu::Qword raw_path_size = frame.argument(size_argument);
    if (!syscall_path_size_is_valid(raw_path_size))
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::INVALID_ARGUMENT,
            mnos::os::kernel::SyscallResult::INVALID_ARGUMENT);
    }

    return read_syscall_path(
        process,
        thread,
        frame,
        syscall_virtual_address(frame.argument(address_argument)),
        syscall_path_size(raw_path_size),
        path);
}

[[nodiscard]] mnos::os::kernel::SyscallResult check_user_output_buffer(
    mnos::os::proc::Process& process,
    mnos::os::sched::ThreadContext& thread,
    mnos::os::kernel::SyscallFrame& frame,
    const mnos::os::mm::VirtualAddress buffer_address,
    const std::size_t byte_count)
{
    if (byte_count == std::size_t{0})
    {
        return mnos::os::kernel::SyscallResult::HANDLED;
    }
    if (!is_valid_user_io_range(buffer_address, byte_count))
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_ADDRESS,
            mnos::os::kernel::SyscallResult::BAD_ADDRESS);
    }
    try
    {
        check_user_io_access(
            process,
            thread,
            buffer_address,
            byte_count,
            mnos::cpu::memory::MemoryAccessKind::WRITE);
        return mnos::os::kernel::SyscallResult::HANDLED;
    }
    catch (const mnos::cpu::memory::PageFault&)
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_ADDRESS,
            mnos::os::kernel::SyscallResult::BAD_ADDRESS);
    }
    catch (const std::out_of_range&)
    {
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_ADDRESS,
            mnos::os::kernel::SyscallResult::BAD_ADDRESS);
    }
}

[[nodiscard]] mnos::os::kernel::SyscallResult finish_io_status_error(
    mnos::os::kernel::SyscallFrame& frame,
    const mnos::os::io::IoStatus status) noexcept
{
    switch (status)
    {
    case mnos::os::io::IoStatus::BAD_DESCRIPTOR:
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_FILE_DESCRIPTOR,
            mnos::os::kernel::SyscallResult::BAD_DESCRIPTOR);
    case mnos::os::io::IoStatus::BAD_ADDRESS:
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::BAD_ADDRESS,
            mnos::os::kernel::SyscallResult::BAD_ADDRESS);
    case mnos::os::io::IoStatus::INVALID_ARGUMENT:
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::INVALID_ARGUMENT,
            mnos::os::kernel::SyscallResult::INVALID_ARGUMENT);
    case mnos::os::io::IoStatus::NO_SPACE:
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::NO_SPACE,
            mnos::os::kernel::SyscallResult::NO_SPACE);
    case mnos::os::io::IoStatus::READY:
    case mnos::os::io::IoStatus::BLOCKED:
    case mnos::os::io::IoStatus::COUNT:
    default:
        return finish_syscall_error(
            frame,
            mnos::os::kernel::SyscallError::INVALID_ARGUMENT,
            mnos::os::kernel::SyscallResult::INVALID_ARGUMENT);
    }
}
}

namespace mnos::os::kernel
{
SyscallResult Kernel::dispatch_syscall(sched::ThreadContext& thread)
{
    return this->dispatch_syscall_for_process(nullptr, thread);
}

SyscallResult Kernel::dispatch_syscall(proc::Process& process, sched::ThreadContext& thread)
{
    return this->dispatch_syscall_for_process(&process, thread);
}

SyscallResult Kernel::handle_user_syscall(
    proc::Process& process,
    sched::ThreadContext& thread,
    cpu::system::TrapController& trap_controller)
{
    this->require_stage11_services();
    if (!is_pending_user_syscall(thread.cpu_state()))
    {
        return SyscallResult::INVALID_CONTEXT;
    }

    static_cast<void>(thread.snapshot_pending_trap_frame());
    const SyscallResult result = this->dispatch_syscall(process, thread);
    if (!thread.is_alive())
    {
        thread.cpu_state().clear_pending_trap();
        return result;
    }

    if (thread.cpu_state().has_pending_trap())
    {
        trap_controller.return_from_syscall(thread.cpu_state());
    }
    return result;
}

UserTrapResult Kernel::handle_user_page_fault(
    proc::Process& process,
    sched::ThreadContext& thread,
    cpu::system::TrapController& trap_controller)
{
    this->require_stage11_services();
    if (!is_pending_user_page_fault(thread.cpu_state()))
    {
        return UserTrapResult::NOT_USER_TRAP;
    }

    static_cast<void>(thread.snapshot_pending_trap_frame());
    const cpu::system::TrapFrame frame = thread.cpu_state().pending_trap();
    const mm::VirtualAddress fault_address{
        static_cast<mm::AddressValue>(thread.cpu_state().paging().page_fault_linear_address())};
    if (!mm::is_user_address(fault_address))
    {
        return this->kill_user_trap_thread(process, thread);
    }

    const proc::CowFaultResult cow_result = this->handle_cow_write_fault(process, thread);
    if (cow_result == proc::CowFaultResult::COPIED || cow_result == proc::CowFaultResult::RESTORED)
    {
        trap_controller.restore_trap_frame(thread.cpu_state(), frame);
        return UserTrapResult::HANDLED;
    }
    if (cow_result == proc::CowFaultResult::OUT_OF_MEMORY)
    {
        return UserTrapResult::OUT_OF_MEMORY;
    }

    const mm::PageFaultResult page_fault_result = this->handle_page_fault(process, thread);
    if (page_fault_result == mm::PageFaultResult::HANDLED)
    {
        process.address_space().activate(
            thread.cpu_state(),
            proc::process_context_id_for(process.id()),
            cpu::memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
        trap_controller.restore_trap_frame(thread.cpu_state(), frame);
        return UserTrapResult::HANDLED;
    }
    if (page_fault_result == mm::PageFaultResult::OUT_OF_MEMORY)
    {
        return UserTrapResult::OUT_OF_MEMORY;
    }
    if (page_fault_result == mm::PageFaultResult::PROTECTION_FAULT)
    {
        return this->kill_user_trap_thread(process, thread);
    }
    return UserTrapResult::NOT_USER_TRAP;
}

UserMapResult Kernel::map_user_zero_page(
    proc::Process& process,
    sched::ThreadContext& thread,
    const mm::VirtualAddress virtual_page)
{
    this->require_stage11_services();
    if (!is_valid_user_page(virtual_page))
    {
        return UserMapResult::INVALID_ADDRESS;
    }
    if (user_page_is_mapped(process, virtual_page))
    {
        return UserMapResult::ALREADY_MAPPED;
    }

    mm::PhysicalAddress physical_page;
    try
    {
        physical_page = this->physical_page_allocator_.value().allocate_page();
    }
    catch (const std::bad_alloc&)
    {
        return UserMapResult::OUT_OF_MEMORY;
    }

    try
    {
        this->zero_physical_page(physical_page);
        process.address_space().map_page(
            virtual_page,
            physical_page,
            cpu::memory::PagePermissions::user_read_write_no_execute());
        process.address_space().activate(
            thread.cpu_state(),
            proc::process_context_id_for(process.id()),
            cpu::memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    }
    catch (const std::bad_alloc&)
    {
        this->physical_page_allocator_.value().free_page(physical_page);
        return UserMapResult::OUT_OF_MEMORY;
    }
    catch (const std::out_of_range&)
    {
        this->physical_page_allocator_.value().free_page(physical_page);
        return UserMapResult::OUT_OF_MEMORY;
    }
    return UserMapResult::MAPPED;
}

SyscallResult Kernel::dispatch_syscall_for_process(proc::Process* process, sched::ThreadContext& thread)
{
    this->require_stage11_services();
    SyscallFrame frame{thread.cpu_state()};
    switch (frame.number())
    {
    case SyscallNumber::YIELD:
        if (this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
        {
            static_cast<void>(this->scheduler_.yield_current());
        }
        finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::HANDLED;
    case SyscallNumber::EXIT:
        finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
        if (process != nullptr)
        {
            this->exit_process(*process, static_cast<std::int64_t>(frame.argument(SyscallArgument::ARG0)));
        }
        else if (this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
        {
            static_cast<void>(this->scheduler_.exit_current());
        }
        else
        {
            thread.set_state(sched::ThreadState::DEAD);
        }
        return SyscallResult::HANDLED;
    case SyscallNumber::GETPID:
        return this->dispatch_getpid(process, frame);
    case SyscallNumber::MAP_ANON_PAGE:
        return this->dispatch_map_anon_page(process, thread, frame);
    case SyscallNumber::FORK_COW:
        return this->dispatch_fork_cow(process, frame);
    case SyscallNumber::FUTEX_WAIT:
        return this->dispatch_futex_wait(process, thread, frame);
    case SyscallNumber::FUTEX_WAKE_ONE:
        return this->dispatch_futex_wake_one(process, frame);
    case SyscallNumber::FUTEX_WAKE_ALL:
        return this->dispatch_futex_wake_all(process, frame);
    case SyscallNumber::READ:
        return this->dispatch_read(process, thread, frame);
    case SyscallNumber::WRITE:
        return this->dispatch_write(process, thread, frame);
    case SyscallNumber::OPEN:
        return this->dispatch_open(process, thread, frame);
    case SyscallNumber::CLOSE:
        return this->dispatch_close(process, frame);
    case SyscallNumber::STAT:
        return this->dispatch_stat(process, thread, frame);
    case SyscallNumber::READDIR:
        return this->dispatch_readdir(process, thread, frame);
    case SyscallNumber::WAIT:
        return this->dispatch_wait(process, thread, frame);
    case SyscallNumber::COUNT:
        return finish_syscall_error(frame, SyscallError::NO_SYS, SyscallResult::UNSUPPORTED);
    }
}

SyscallResult Kernel::dispatch_getpid(proc::Process* process, SyscallFrame& frame) noexcept
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    finish_syscall_success(frame, static_cast<cpu::Qword>(process->id().value()));
    return SyscallResult::HANDLED;
}

SyscallResult Kernel::dispatch_map_anon_page(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const UserMapResult map_result =
        this->map_user_zero_page(*process, thread, syscall_virtual_address(frame.argument(SyscallArgument::ARG0)));
    switch (map_result)
    {
    case UserMapResult::MAPPED:
        finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::HANDLED;
    case UserMapResult::INVALID_ADDRESS:
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    case UserMapResult::ALREADY_MAPPED:
        return finish_syscall_error(frame, SyscallError::ALREADY_EXISTS, SyscallResult::INVALID_ARGUMENT);
    case UserMapResult::OUT_OF_MEMORY:
        return finish_syscall_error(frame, SyscallError::NO_MEMORY, SyscallResult::OUT_OF_MEMORY);
    case UserMapResult::COUNT:
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }
}

SyscallResult Kernel::dispatch_fork_cow(proc::Process* process, SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const mm::VirtualAddress first_page = syscall_virtual_address(frame.argument(SyscallArgument::ARG0));
    const cpu::Qword raw_page_count = frame.argument(SyscallArgument::ARG1);
    if (raw_page_count == cpu::Qword{0} || raw_page_count > SYSCALL_FORK_MAX_COW_PAGE_COUNT ||
        !is_valid_user_page(first_page))
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }

    const mm::AddressValue page_count = static_cast<mm::AddressValue>(raw_page_count);
    if (page_count > (std::numeric_limits<mm::AddressValue>::max() - first_page.value()) / mm::MM_PAGE_SIZE_BYTES)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }

    std::vector<mm::VirtualAddress> cow_pages;
    try
    {
        cow_pages.reserve(static_cast<std::size_t>(page_count));
        for (mm::AddressValue page_index = mm::AddressValue{0}; page_index < page_count; ++page_index)
        {
            const mm::VirtualAddress page = first_page + (page_index * mm::MM_PAGE_SIZE_BYTES);
            if (!is_valid_user_page(page))
            {
                return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
            }
            const cpu::memory::PageTranslation translation = process->address_space().page_translation(
                page,
                cpu::memory::MemoryAccessKind::READ,
                cpu::system::PrivilegeLevel::RING3);
            if (translation.page_size_bytes() != cpu::memory::PAGE_SIZE_4K_BYTES)
            {
                return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
            }
            cow_pages.push_back(page);
        }

        proc::Process& child_process = this->fork_process_cow(*process, cow_pages);
        finish_syscall_success(frame, static_cast<cpu::Qword>(child_process.id().value()));
        return SyscallResult::HANDLED;
    }
    catch (const std::bad_alloc&)
    {
        return finish_syscall_error(frame, SyscallError::NO_MEMORY, SyscallResult::OUT_OF_MEMORY);
    }
    catch (const cpu::memory::PageFault&)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }
    catch (const std::invalid_argument&)
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }
    catch (const std::out_of_range&)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }
    catch (const std::logic_error&)
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }
}

SyscallResult Kernel::dispatch_futex_wait(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const mm::VirtualAddress address = syscall_virtual_address(frame.argument(SyscallArgument::ARG0));
    if (!is_valid_user_futex_word(address) || (address.value() % proc::FUTEX_WORD_ALIGNMENT_BYTES) != mm::AddressValue{0})
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }

    cpu::Qword current_word = cpu::Qword{0};
    try
    {
        const cpu::memory::PageTranslation translation = process->address_space().page_translation(
            address,
            cpu::memory::MemoryAccessKind::READ,
            cpu::system::PrivilegeLevel::RING3);
        current_word = process->address_space().memory_bus().read(
            translation.translate(mm::to_cpu_address(address)),
            cpu::DataSize::DWORD) & SYSCALL_FUTEX_WORD_MASK;
    }
    catch (const cpu::memory::PageFault&)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }

    const cpu::Qword expected_word = frame.argument(SyscallArgument::ARG1) & SYSCALL_FUTEX_WORD_MASK;
    if (current_word != expected_word)
    {
        return finish_syscall_error(frame, SyscallError::AGAIN, SyscallResult::HANDLED);
    }

    static_cast<void>(this->wait_on_futex(*process, address, thread));
    finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
    return SyscallResult::BLOCKED;
}

SyscallResult Kernel::dispatch_futex_wake_one(proc::Process* process, SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const mm::VirtualAddress address = syscall_virtual_address(frame.argument(SyscallArgument::ARG0));
    if (!is_valid_user_futex_word(address) || (address.value() % proc::FUTEX_WORD_ALIGNMENT_BYTES) != mm::AddressValue{0})
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }

    sched::ThreadContext* const thread = this->wake_one_futex(*process, address);
    finish_syscall_success(frame, thread == nullptr ? cpu::Qword{0} : cpu::Qword{1});
    return SyscallResult::HANDLED;
}

SyscallResult Kernel::dispatch_futex_wake_all(proc::Process* process, SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const mm::VirtualAddress address = syscall_virtual_address(frame.argument(SyscallArgument::ARG0));
    if (!is_valid_user_futex_word(address) || (address.value() % proc::FUTEX_WORD_ALIGNMENT_BYTES) != mm::AddressValue{0})
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }

    const std::vector<sched::ThreadContext*> threads = this->wake_all_futex(*process, address);
    finish_syscall_success(frame, static_cast<cpu::Qword>(threads.size()));
    return SyscallResult::HANDLED;
}

SyscallResult Kernel::dispatch_read(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const io::FileDescriptor descriptor = syscall_file_descriptor(frame.argument(SyscallArgument::ARG0));
    if (!process->file_descriptors().readable(descriptor))
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_DESCRIPTOR);
    }

    const cpu::Qword raw_byte_count = frame.argument(SyscallArgument::ARG2);
    if (!syscall_transfer_size_is_valid(raw_byte_count))
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }

    const std::size_t byte_count = syscall_transfer_size(raw_byte_count);
    if (byte_count == std::size_t{0})
    {
        finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::HANDLED;
    }

    const mm::VirtualAddress buffer_address = syscall_virtual_address(frame.argument(SyscallArgument::ARG1));
    if (!is_valid_user_io_range(buffer_address, byte_count))
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_ADDRESS);
    }

    try
    {
        check_user_io_access(
            *process,
            thread,
            buffer_address,
            byte_count,
            cpu::memory::MemoryAccessKind::WRITE);
        std::vector<char> buffer(byte_count);
        const io::IoResult io_result = this->read_fd(*process, thread, descriptor, buffer);
        if (io_result.is_blocked())
        {
            finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
            return SyscallResult::BLOCKED;
        }
        if (!io_result.is_ready())
        {
            return finish_io_status_error(frame, io_result.status());
        }

        write_user_bytes(
            *process,
            thread,
            buffer_address,
            std::span<const char>{buffer.data(), io_result.byte_count()});
        finish_syscall_success(frame, static_cast<cpu::Qword>(io_result.byte_count()));
        return SyscallResult::HANDLED;
    }
    catch (const std::bad_alloc&)
    {
        return finish_syscall_error(frame, SyscallError::NO_MEMORY, SyscallResult::OUT_OF_MEMORY);
    }
    catch (const cpu::memory::PageFault&)
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_ADDRESS);
    }
    catch (const std::out_of_range&)
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_ADDRESS);
    }
}

SyscallResult Kernel::dispatch_write(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const io::FileDescriptor descriptor = syscall_file_descriptor(frame.argument(SyscallArgument::ARG0));
    if (!process->file_descriptors().writable(descriptor))
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_DESCRIPTOR);
    }

    const cpu::Qword raw_byte_count = frame.argument(SyscallArgument::ARG2);
    if (!syscall_transfer_size_is_valid(raw_byte_count))
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }

    const std::size_t byte_count = syscall_transfer_size(raw_byte_count);
    if (byte_count == std::size_t{0})
    {
        finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::HANDLED;
    }

    const mm::VirtualAddress buffer_address = syscall_virtual_address(frame.argument(SyscallArgument::ARG1));
    if (!is_valid_user_io_range(buffer_address, byte_count))
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_ADDRESS);
    }

    try
    {
        check_user_io_access(
            *process,
            thread,
            buffer_address,
            byte_count,
            cpu::memory::MemoryAccessKind::READ);
        const std::vector<char> buffer = read_user_bytes(*process, thread, buffer_address, byte_count);
        const io::IoResult io_result = this->write_fd(
            *process,
            descriptor,
            std::string_view{buffer.data(), buffer.size()});
        if (!io_result.is_ready())
        {
            return finish_io_status_error(frame, io_result.status());
        }

        finish_syscall_success(frame, static_cast<cpu::Qword>(io_result.byte_count()));
        return SyscallResult::HANDLED;
    }
    catch (const std::bad_alloc&)
    {
        return finish_syscall_error(frame, SyscallError::NO_MEMORY, SyscallResult::OUT_OF_MEMORY);
    }
    catch (const cpu::memory::PageFault&)
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_ADDRESS);
    }
    catch (const std::out_of_range&)
    {
        return finish_io_status_error(frame, io::IoStatus::BAD_ADDRESS);
    }
}

SyscallResult Kernel::dispatch_open(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const cpu::Qword raw_flags = frame.argument(SyscallArgument::ARG2);
    if (!syscall_open_flags_are_valid(raw_flags))
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }

    std::string path;
    const SyscallResult path_result =
        read_syscall_path_argument(*process, thread, frame, SyscallArgument::ARG0, SyscallArgument::ARG1, path);
    if (path_result != SyscallResult::HANDLED)
    {
        return path_result;
    }

    try
    {
        const std::optional<fs::VfsNode> node = this->vfs().lookup(path);
        if (node.has_value() && node->is_directory())
        {
            return finish_syscall_error(frame, SyscallError::IS_DIRECTORY, SyscallResult::INVALID_ARGUMENT);
        }

        const io::FileDescriptor descriptor = this->open_file(
            *process,
            path,
            syscall_open_access_mode(raw_flags),
            syscall_open_create_requested(raw_flags));
        finish_syscall_success(frame, static_cast<cpu::Qword>(descriptor.value()));
        return SyscallResult::HANDLED;
    }
    catch (const std::bad_alloc&)
    {
        return finish_syscall_error(frame, SyscallError::NO_MEMORY, SyscallResult::OUT_OF_MEMORY);
    }
    catch (const std::length_error&)
    {
        return finish_syscall_error(frame, SyscallError::NO_SPACE, SyscallResult::NO_SPACE);
    }
    catch (const std::out_of_range&)
    {
        return finish_syscall_error(frame, SyscallError::NO_ENTRY, SyscallResult::NOT_FOUND);
    }
    catch (const std::invalid_argument&)
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }
}

SyscallResult Kernel::dispatch_close(proc::Process* process, SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const io::FileDescriptor descriptor = syscall_file_descriptor(frame.argument(SyscallArgument::ARG0));
    if (!this->close_fd(*process, descriptor))
    {
        return finish_syscall_error(frame, SyscallError::BAD_FILE_DESCRIPTOR, SyscallResult::BAD_DESCRIPTOR);
    }

    finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
    return SyscallResult::HANDLED;
}

SyscallResult Kernel::dispatch_stat(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    std::string path;
    const SyscallResult path_result =
        read_syscall_path_argument(*process, thread, frame, SyscallArgument::ARG0, SyscallArgument::ARG1, path);
    if (path_result != SyscallResult::HANDLED)
    {
        return path_result;
    }

    const mm::VirtualAddress out_address = syscall_virtual_address(frame.argument(SyscallArgument::ARG2));
    const SyscallResult output_result =
        check_user_output_buffer(*process, thread, frame, out_address, SYSCALL_STAT_RECORD_SIZE_BYTES);
    if (output_result != SyscallResult::HANDLED)
    {
        return output_result;
    }

    try
    {
        const std::optional<fs::VfsNode> node = this->vfs().lookup(path);
        if (!node.has_value())
        {
            return finish_syscall_error(frame, SyscallError::NO_ENTRY, SyscallResult::NOT_FOUND);
        }

        const std::array<char, SYSCALL_STAT_RECORD_SIZE_BYTES> record = serialize_stat_record(node.value());
        write_user_bytes(*process, thread, out_address, record);
        finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::HANDLED;
    }
    catch (const std::bad_alloc&)
    {
        return finish_syscall_error(frame, SyscallError::NO_MEMORY, SyscallResult::OUT_OF_MEMORY);
    }
    catch (const cpu::memory::PageFault&)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }
    catch (const std::out_of_range&)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }
    catch (const std::invalid_argument&)
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }
}

SyscallResult Kernel::dispatch_readdir(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const cpu::Qword raw_output_size = frame.argument(SyscallArgument::ARG3);
    if (!syscall_transfer_size_is_valid(raw_output_size))
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }

    std::string path;
    const SyscallResult path_result =
        read_syscall_path_argument(*process, thread, frame, SyscallArgument::ARG0, SyscallArgument::ARG1, path);
    if (path_result != SyscallResult::HANDLED)
    {
        return path_result;
    }

    const std::size_t output_size = syscall_transfer_size(raw_output_size);
    const mm::VirtualAddress out_address = syscall_virtual_address(frame.argument(SyscallArgument::ARG2));

    try
    {
        const std::optional<fs::VfsNode> node = this->vfs().lookup(path);
        if (!node.has_value())
        {
            return finish_syscall_error(frame, SyscallError::NO_ENTRY, SyscallResult::NOT_FOUND);
        }
        if (!node->is_directory())
        {
            return finish_syscall_error(frame, SyscallError::NOT_DIRECTORY, SyscallResult::INVALID_ARGUMENT);
        }

        const std::vector<fs::SimpleFsDirectoryEntry> entries = this->vfs().read_directory(path);
        const std::size_t capacity = output_size / SYSCALL_DIRENT_RECORD_SIZE_BYTES;
        const std::size_t record_count = std::min(capacity, entries.size());
        const std::size_t byte_count = record_count * SYSCALL_DIRENT_RECORD_SIZE_BYTES;
        const SyscallResult output_result = check_user_output_buffer(*process, thread, frame, out_address, byte_count);
        if (output_result != SyscallResult::HANDLED)
        {
            return output_result;
        }

        std::vector<char> output(byte_count);
        for (std::size_t record_index = std::size_t{0}; record_index < record_count; ++record_index)
        {
            const std::array<char, SYSCALL_DIRENT_RECORD_SIZE_BYTES> record =
                serialize_dirent_record(entries[record_index]);
            std::copy(
                record.begin(),
                record.end(),
                output.begin() +
                    static_cast<std::ptrdiff_t>(record_index * SYSCALL_DIRENT_RECORD_SIZE_BYTES));
        }
        write_user_bytes(*process, thread, out_address, output);
        finish_syscall_success(frame, static_cast<cpu::Qword>(byte_count));
        return SyscallResult::HANDLED;
    }
    catch (const std::bad_alloc&)
    {
        return finish_syscall_error(frame, SyscallError::NO_MEMORY, SyscallResult::OUT_OF_MEMORY);
    }
    catch (const cpu::memory::PageFault&)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }
    catch (const std::out_of_range&)
    {
        return finish_syscall_error(frame, SyscallError::BAD_ADDRESS, SyscallResult::BAD_ADDRESS);
    }
    catch (const std::invalid_argument&)
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }
}

SyscallResult Kernel::dispatch_wait(
    proc::Process* process,
    sched::ThreadContext& thread,
    SyscallFrame& frame)
{
    if (process == nullptr)
    {
        return finish_syscall_error(frame, SyscallError::OPERATION_NOT_SUPPORTED, SyscallResult::INVALID_CONTEXT);
    }

    const proc::ProcessId child_id{
        static_cast<proc::ProcessId::value_type>(frame.argument(SyscallArgument::ARG0))};
    if (!child_id.is_valid())
    {
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }

    const ProcessWaitResult wait_result = this->wait_process(*process, thread, child_id);
    switch (wait_result.status())
    {
    case ProcessWaitStatus::EXITED:
        finish_syscall_success(frame, static_cast<cpu::Qword>(wait_result.exit_code()));
        return SyscallResult::HANDLED;
    case ProcessWaitStatus::BLOCKED:
        finish_syscall_success(frame, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::BLOCKED;
    case ProcessWaitStatus::NO_CHILD:
        return finish_syscall_error(frame, SyscallError::NO_ENTRY, SyscallResult::NOT_FOUND);
    case ProcessWaitStatus::COUNT:
        return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
    }
    return finish_syscall_error(frame, SyscallError::INVALID_ARGUMENT, SyscallResult::INVALID_ARGUMENT);
}

void Kernel::require_stage11_services() const
{
    this->require_booted();
    if (!this->has_stage11_services())
    {
        throw std::logic_error{KERNEL_STAGE11_NOT_READY_MESSAGE};
    }
}

UserTrapResult Kernel::kill_user_trap_thread(proc::Process& process, sched::ThreadContext& thread)
{
    if (thread.cpu_state().has_pending_trap())
    {
        thread.cpu_state().clear_pending_trap();
    }
    this->exit_process(process, KERNEL_USER_TRAP_KILLED_EXIT_CODE);
    return UserTrapResult::KILLED;
}

void Kernel::zero_physical_page(const mm::PhysicalAddress physical_address)
{
    for (mm::AddressValue offset = mm::AddressValue{0}; offset < mm::MM_PAGE_SIZE_BYTES; offset += cpu::DATA_SIZE_QWORD_BYTES)
    {
        this->boot_context_->memory_bus().write(
            mm::to_cpu_address(physical_address + offset),
            cpu::DataSize::QWORD,
            cpu::Qword{0});
    }
}
}
