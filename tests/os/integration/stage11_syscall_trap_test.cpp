#include <array>
#include <cstddef>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/descriptor_tables.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/trap_controller.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process_context.hpp>
#include <mnos/os/proc/user_loader.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{512});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr cpu::Address64 TEST_SYSCALL_ENTRY_RIP = cpu::Address64{0xFFFF8000'00001000ULL};
constexpr cpu::InstructionPointer TEST_USER_RETURN_RIP = cpu::InstructionPointer{0x400010};
constexpr cpu::InstructionPointer TEST_USER_FAULT_RIP = cpu::InstructionPointer{0x400020};
constexpr cpu::InstructionPointer TEST_PAGE_FAULT_HANDLER_RIP = cpu::InstructionPointer{0xFFFF8000'00002000ULL};
constexpr cpu::Qword TEST_KERNEL_STACK = cpu::Qword{0xFFFF8000'00010000ULL};
constexpr mm::VirtualAddress TEST_TEXT_BASE = mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mm::VirtualAddress TEST_DATA_BASE = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::VirtualAddress TEST_ANON_PAGE = mm::ADDRESS_LAYOUT_USER_HEAP_BASE + mm::MM_PAGE_SIZE_BYTES;
constexpr mm::AddressValue TEST_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr cpu::Byte TEST_TEXT_BYTE = cpu::Byte{0x90};
constexpr cpu::Byte TEST_DATA_BYTE = cpu::Byte{0x7F};
constexpr cpu::Qword TEST_PARENT_DATA_VALUE = cpu::Qword{0x0102030405060708ULL};
constexpr cpu::Qword TEST_FUTEX_READY_VALUE = cpu::Qword{0};
constexpr cpu::Qword TEST_FUTEX_MISMATCH_VALUE = cpu::Qword{1};

[[nodiscard]] proc::UserProgram make_program()
{
    proc::UserProgram program{TEST_TEXT_BASE};
    program.set_initial_stack_size_bytes(TEST_STACK_SIZE_BYTES);
    program.add_segment(proc::UserSegment::text(TEST_TEXT_BASE, std::vector<cpu::Byte>{TEST_TEXT_BYTE}));
    program.add_segment(proc::UserSegment::data(TEST_DATA_BASE, std::vector<cpu::Byte>{TEST_DATA_BYTE}));
    return program;
}

[[nodiscard]] cpu_system::TrapController make_trap_controller()
{
    cpu_system::TrapController controller;
    controller.configure_syscall(cpu_system::SyscallDescriptor::enabled(TEST_SYSCALL_ENTRY_RIP));
    controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_KERNEL_STACK);
    controller.idt().set_gate(
        cpu_system::InterruptVector::page_fault(),
        cpu_system::InterruptGate::interrupt_gate(TEST_PAGE_FAULT_HANDLER_RIP, cpu_system::PrivilegeLevel::RING0));
    return controller;
}

void write_syscall_number(sched::ThreadContext& thread, const kernel::SyscallNumber number)
{
    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(number));
}

void enter_syscall(sched::ThreadContext& thread, cpu_system::TrapController& controller)
{
    static_cast<void>(controller.enter_syscall(thread.cpu_state(), TEST_USER_RETURN_RIP));
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

void write_user_qword(
    platform::Machine& machine,
    proc::Process& process,
    const mm::VirtualAddress address,
    const cpu::Qword value)
{
    const cpu_memory::PageTranslation translation = process.address_space().page_translation(
        address,
        cpu_memory::MemoryAccessKind::WRITE,
        cpu_system::PrivilegeLevel::RING3);
    machine.memory_bus().write(translation.translate(mm::to_cpu_address(address)), cpu::DataSize::QWORD, value);
}

void raise_user_page_fault(
    sched::ThreadContext& thread,
    cpu_system::TrapController& controller,
    const mm::VirtualAddress fault_address,
    const cpu::Qword error_code)
{
    thread.cpu_state().set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    thread.cpu_state().set_rip(TEST_USER_FAULT_RIP);
    thread.cpu_state().registers().write(cpu::RegisterId::RSP, mm::ADDRESS_LAYOUT_USER_STACK_TOP.value());
    thread.cpu_state().paging().set_page_fault_linear_address(mm::to_cpu_address(fault_address));
    static_cast<void>(controller.raise_exception(
        thread.cpu_state(),
        cpu_system::InterruptVector::page_fault(),
        error_code));
}

void raise_user_write_page_fault(
    sched::ThreadContext& thread,
    cpu_system::TrapController& controller,
    const mm::VirtualAddress fault_address)
{
    raise_user_page_fault(
        thread,
        controller,
        fault_address,
        cpu_memory::PAGE_FAULT_ERROR_PRESENT_BIT |
            cpu_memory::PAGE_FAULT_ERROR_WRITE_BIT |
            cpu_memory::PAGE_FAULT_ERROR_USER_BIT);
}

void raise_user_not_present_page_fault(
    sched::ThreadContext& thread,
    cpu_system::TrapController& controller,
    const mm::VirtualAddress fault_address)
{
    raise_user_page_fault(
        thread,
        controller,
        fault_address,
        cpu_memory::PAGE_FAULT_ERROR_WRITE_BIT | cpu_memory::PAGE_FAULT_ERROR_USER_BIT);
}
}

TEST(Stage11SyscallTrapIntegrationTest, CompletesUserSyscallsAndMapsAnonPage)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    ASSERT_TRUE(os_kernel.has_stage11_services());
    cpu_system::TrapController controller = make_trap_controller();

    proc::UserProgram program = make_program();
    proc::Process& process = os_kernel.create_user_process(program);
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});
    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);

    write_syscall_number(thread, kernel::SyscallNumber::GETPID);
    enter_syscall(thread, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, thread, controller), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(process.id().value())));
    EXPECT_THAT(thread.cpu_state().rip(), Eq(TEST_USER_RETURN_RIP));
    EXPECT_THAT(thread.cpu_state().privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_FALSE(thread.cpu_state().has_pending_trap());
    ASSERT_TRUE(thread.has_last_trap_frame());
    EXPECT_THAT(thread.last_trap_frame().kind(), Eq(cpu_system::TrapKind::SYSCALL));

    write_syscall_number(thread, kernel::SyscallNumber::MAP_ANON_PAGE);
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_ANON_PAGE.value());
    enter_syscall(thread, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, thread, controller), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::SYSCALL_SUCCESS_RESULT));

    const cpu_memory::PageTranslation anon_translation = process.address_space().page_translation(
        TEST_ANON_PAGE,
        cpu_memory::MemoryAccessKind::WRITE,
        cpu_system::PrivilegeLevel::RING3);
    EXPECT_TRUE(anon_translation.permissions().writable());
    EXPECT_TRUE(anon_translation.permissions().user_accessible());
    EXPECT_FALSE(anon_translation.permissions().executable());
    EXPECT_THAT(machine.memory_bus().read(anon_translation.physical_frame_base(), cpu::DataSize::QWORD), Eq(cpu::Qword{0}));

    write_syscall_number(thread, kernel::SyscallNumber::MAP_ANON_PAGE);
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_ANON_PAGE.value());
    enter_syscall(thread, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, thread, controller), Eq(kernel::SyscallResult::INVALID_ARGUMENT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::ALREADY_EXISTS)));
    EXPECT_THAT(thread.cpu_state().privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
}

TEST(Stage11SyscallTrapIntegrationTest, ReportsInvalidSyscallContextsAndArguments)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    cpu_system::TrapController controller = make_trap_controller();

    proc::UserProgram program = make_program();
    proc::Process& process = os_kernel.create_user_process(program);
    sched::ThreadContext& thread = process.thread_at(std::size_t{0});

    EXPECT_THAT(os_kernel.handle_user_syscall(process, thread, controller), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    EXPECT_THAT(os_kernel.handle_user_page_fault(process, thread, controller), Eq(kernel::UserTrapResult::NOT_USER_TRAP));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::GETPID));
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::OPERATION_NOT_SUPPORTED)));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::MAP_ANON_PAGE));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::MAP_ANON_PAGE));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_ADDRESS)));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FORK_COW));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    thread.cpu_state().registers().write(cpu::RegisterId::RSI, cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FORK_COW));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    thread.cpu_state().registers().write(cpu::RegisterId::RSI, cpu::Qword{0});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FORK_COW));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_ANON_PAGE.value());
    thread.cpu_state().registers().write(cpu::RegisterId::RSI, cpu::Qword{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FUTEX_WAIT));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value() + mm::AddressValue{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FUTEX_WAIT));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_ANON_PAGE.value());
    thread.cpu_state().registers().write(cpu::RegisterId::RSI, cpu::Qword{0});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FUTEX_WAIT));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FUTEX_WAKE_ONE));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value() + mm::AddressValue{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FUTEX_WAKE_ALL));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value() + mm::AddressValue{1});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::BAD_ADDRESS));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FUTEX_WAKE_ALL));
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, cpu::Qword{99});
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::UNSUPPORTED));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(kernel::SYSCALL_UNSUPPORTED_RESULT));

    write_syscall_number(thread, kernel::SyscallNumber::EXIT);
    enter_syscall(thread, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, thread, controller), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(thread.state(), Eq(sched::ThreadState::DEAD));
    EXPECT_FALSE(thread.cpu_state().has_pending_trap());
}

TEST(Stage11SyscallTrapIntegrationTest, MapAnonPageReportsOutOfMemory)
{
    constexpr std::size_t TEST_SMALL_MEMORY_SIZE_BYTES =
        static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{64});

    platform::Machine machine(TEST_SMALL_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& thread = os_kernel.create_thread(process);

    while (!os_kernel.physical_page_allocator().empty())
    {
        static_cast<void>(os_kernel.physical_page_allocator().allocate_page());
    }

    EXPECT_THAT(
        os_kernel.map_user_zero_page(process, thread, TEST_ANON_PAGE),
        Eq(kernel::UserMapResult::OUT_OF_MEMORY));

    write_syscall_number(thread, kernel::SyscallNumber::MAP_ANON_PAGE);
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_ANON_PAGE.value());
    EXPECT_THAT(os_kernel.dispatch_syscall(process, thread), Eq(kernel::SyscallResult::OUT_OF_MEMORY));
    EXPECT_THAT(
        thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::NO_MEMORY)));
}

TEST(Stage11SyscallTrapIntegrationTest, FutexWaitBlocksAndWakeOneReadiesThread)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    cpu_system::TrapController controller = make_trap_controller();

    proc::UserProgram program = make_program();
    proc::Process& process = os_kernel.create_user_process(program);
    sched::ThreadContext& waiter = process.thread_at(std::size_t{0});
    sched::ThreadContext& waker = os_kernel.create_thread(process);
    initialize_user_like_thread(process, waker);
    write_user_qword(machine, process, TEST_DATA_BASE, TEST_FUTEX_READY_VALUE);
    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);
    ASSERT_EQ(&os_kernel.scheduler().current(), &waiter);

    write_syscall_number(waiter, kernel::SyscallNumber::FUTEX_WAIT);
    waiter.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    waiter.cpu_state().registers().write(cpu::RegisterId::RSI, TEST_FUTEX_MISMATCH_VALUE);
    enter_syscall(waiter, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, waiter, controller), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(
        waiter.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::AGAIN)));
    EXPECT_THAT(waiter.state(), Eq(sched::ThreadState::RUNNING));

    write_syscall_number(waiter, kernel::SyscallNumber::FUTEX_WAIT);
    waiter.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    waiter.cpu_state().registers().write(cpu::RegisterId::RSI, TEST_FUTEX_READY_VALUE);
    enter_syscall(waiter, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, waiter, controller), Eq(kernel::SyscallResult::BLOCKED));
    EXPECT_THAT(waiter.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_THAT(waiter.cpu_state().privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_FALSE(waiter.cpu_state().has_pending_trap());
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(waker.id()));

    write_syscall_number(waker, kernel::SyscallNumber::FUTEX_WAKE_ONE);
    waker.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    enter_syscall(waker, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, waker, controller), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(waker.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{1}));
    EXPECT_THAT(waiter.state(), Eq(sched::ThreadState::READY));
    EXPECT_THAT(os_kernel.futex_table().waiter_count(proc::FutexKey{process.id(), TEST_DATA_BASE}), Eq(std::size_t{0}));

    write_syscall_number(waker, kernel::SyscallNumber::FUTEX_WAKE_ALL);
    waker.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    enter_syscall(waker, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(process, waker, controller), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(waker.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));
}

TEST(Stage11SyscallTrapIntegrationTest, ForkCowSyscallSharesMappedUserPages)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    cpu_system::TrapController controller = make_trap_controller();

    proc::UserProgram program = make_program();
    proc::Process& parent = os_kernel.create_user_process(program);
    sched::ThreadContext& parent_thread = parent.thread_at(std::size_t{0});
    write_user_qword(machine, parent, TEST_DATA_BASE, TEST_PARENT_DATA_VALUE);
    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);

    write_syscall_number(parent_thread, kernel::SyscallNumber::FORK_COW);
    parent_thread.cpu_state().registers().write(cpu::RegisterId::RDI, TEST_DATA_BASE.value());
    parent_thread.cpu_state().registers().write(cpu::RegisterId::RSI, cpu::Qword{1});
    enter_syscall(parent_thread, controller);
    EXPECT_THAT(os_kernel.handle_user_syscall(parent, parent_thread, controller), Eq(kernel::SyscallResult::HANDLED));

    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    proc::Process& child = os_kernel.process_at(std::size_t{1});
    EXPECT_THAT(parent_thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(child.id().value())));
    EXPECT_THAT(os_kernel.copy_on_write_manager().mapping_count(), Eq(std::size_t{2}));
    EXPECT_FALSE(parent.address_space().page_translation(TEST_DATA_BASE).permissions().writable());
    EXPECT_FALSE(child.address_space().page_translation(TEST_DATA_BASE).permissions().writable());
    EXPECT_THAT(parent_thread.cpu_state().privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_FALSE(parent_thread.cpu_state().has_pending_trap());
}

TEST(Stage11SyscallTrapIntegrationTest, UserPageFaultResolvesCowAndKillsInvalidUserFault)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    cpu_system::TrapController controller = make_trap_controller();

    proc::UserProgram program = make_program();
    proc::Process& parent = os_kernel.create_user_process(program);
    write_user_qword(machine, parent, TEST_DATA_BASE, TEST_PARENT_DATA_VALUE);
    const std::array<mm::VirtualAddress, 1> cow_pages{TEST_DATA_BASE};
    proc::Process& child = os_kernel.fork_process_cow(parent, cow_pages);
    sched::ThreadContext& child_thread = os_kernel.create_thread(child);
    initialize_user_like_thread(child, child_thread);

    raise_user_write_page_fault(child_thread, controller, TEST_DATA_BASE);
    EXPECT_THAT(os_kernel.handle_user_page_fault(child, child_thread, controller), Eq(kernel::UserTrapResult::HANDLED));
    EXPECT_FALSE(child_thread.cpu_state().has_pending_trap());
    EXPECT_THAT(child_thread.cpu_state().privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_THAT(child_thread.cpu_state().rip(), Eq(TEST_USER_FAULT_RIP));
    EXPECT_TRUE(child_thread.has_last_trap_frame());
    EXPECT_TRUE(child.address_space().page_translation(TEST_DATA_BASE).permissions().writable());
    EXPECT_THAT(os_kernel.copy_on_write_manager().mapping_count(), Eq(std::size_t{1}));

    sched::ThreadContext& parent_thread = parent.thread_at(std::size_t{0});
    initialize_user_like_thread(parent, parent_thread);
    raise_user_not_present_page_fault(parent_thread, controller, TEST_ANON_PAGE);
    EXPECT_THAT(os_kernel.handle_user_page_fault(parent, parent_thread, controller), Eq(kernel::UserTrapResult::HANDLED));
    EXPECT_FALSE(parent_thread.cpu_state().has_pending_trap());
    EXPECT_THAT(parent_thread.cpu_state().privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(parent.address_space().page_translation(TEST_ANON_PAGE).permissions().writable());

    raise_user_write_page_fault(parent_thread, controller, TEST_TEXT_BASE);
    EXPECT_THAT(os_kernel.handle_user_page_fault(parent, parent_thread, controller), Eq(kernel::UserTrapResult::KILLED));
    EXPECT_THAT(parent_thread.state(), Eq(sched::ThreadState::DEAD));
    EXPECT_FALSE(parent_thread.cpu_state().has_pending_trap());

    proc::Process& invalid_fault_process = os_kernel.create_user_process(program);
    sched::ThreadContext& invalid_fault_thread = invalid_fault_process.thread_at(std::size_t{0});
    initialize_user_like_thread(invalid_fault_process, invalid_fault_thread);
    invalid_fault_thread.cpu_state().paging().set_page_fault_linear_address(mm::to_cpu_address(mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE));
    invalid_fault_thread.cpu_state().set_pending_trap(cpu_system::TrapFrame{
        cpu_system::TrapKind::EXCEPTION,
        cpu_system::InterruptVector::page_fault(),
        TEST_USER_FAULT_RIP,
        TEST_USER_FAULT_RIP,
        cpu::Qword{0},
        mm::ADDRESS_LAYOUT_USER_STACK_TOP.value(),
        cpu_system::PrivilegeLevel::RING3,
        cpu_memory::PAGE_FAULT_ERROR_WRITE_BIT | cpu_memory::PAGE_FAULT_ERROR_USER_BIT});
    EXPECT_THAT(
        os_kernel.handle_user_page_fault(invalid_fault_process, invalid_fault_thread, controller),
        Eq(kernel::UserTrapResult::KILLED));
    EXPECT_THAT(invalid_fault_thread.state(), Eq(sched::ThreadState::DEAD));
    EXPECT_TRUE(invalid_fault_process.is_exited());
    EXPECT_FALSE(invalid_fault_thread.cpu_state().has_pending_trap());
    EXPECT_TRUE(invalid_fault_thread.has_last_trap_frame());
}
