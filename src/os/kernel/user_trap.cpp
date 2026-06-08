#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/trap_controller.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/proc/process_context.hpp>

namespace
{
constexpr const char* KERNEL_STAGE11_NOT_READY_MESSAGE = "kernel stage11 services are not initialized";

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
        return this->kill_user_trap_thread(thread);
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
        return this->kill_user_trap_thread(thread);
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
        if (this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
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

void Kernel::require_stage11_services() const
{
    this->require_booted();
    if (!this->has_stage11_services())
    {
        throw std::logic_error{KERNEL_STAGE11_NOT_READY_MESSAGE};
    }
}

UserTrapResult Kernel::kill_user_trap_thread(sched::ThreadContext& thread)
{
    if (thread.cpu_state().has_pending_trap())
    {
        thread.cpu_state().clear_pending_trap();
    }
    if (this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
    {
        static_cast<void>(this->scheduler_.exit_current());
    }
    else
    {
        thread.set_state(sched::ThreadState::DEAD);
    }
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
