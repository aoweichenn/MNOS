#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/user_loader.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{128});
constexpr mm::PhysicalAddress TEST_ROOT_TABLE{mm::AddressValue{0x1000}};
constexpr mm::PhysicalAddress TEST_NEXT_TABLE{mm::AddressValue{0x2000}};
constexpr mm::PhysicalAddress TEST_TABLE_ARENA_END{mm::AddressValue{0x10000}};
constexpr mm::PhysicalAddress TEST_FIRST_LOADER_PAGE{mm::AddressValue{0x20000}};
constexpr mm::VirtualAddress TEST_TEXT_BASE = mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mm::VirtualAddress TEST_DATA_BASE = mm::ADDRESS_LAYOUT_USER_HEAP_BASE;
constexpr mm::VirtualAddress TEST_KERNEL_STACK_BOTTOM{mm::AddressValue{0x800000}};
constexpr mm::AddressValue TEST_SMALL_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr proc::ProcessId TEST_PROCESS_ID{7};
constexpr cpu::Byte TEST_TEXT_BYTE_0 = cpu::Byte{0x90};
constexpr cpu::Byte TEST_TEXT_BYTE_1 = cpu::Byte{0xC3};
constexpr cpu::Byte TEST_DATA_BYTE_0 = cpu::Byte{0xAA};
constexpr cpu::Byte TEST_DATA_BYTE_1 = cpu::Byte{0x55};
constexpr cpu::Qword TEST_DATA_QWORD = cpu::Qword{0x1122334455667788ULL};
constexpr std::size_t TEST_STACK_STRING_READ_LIMIT = std::size_t{128};
constexpr cpu::Qword TEST_USER_STACK_NULL_POINTER = cpu::Qword{0};
constexpr std::string_view TEST_ARGV0 = "/bin/argc";
constexpr std::string_view TEST_ARGV1 = "alpha";
constexpr std::string_view TEST_ARGV2 = "beta";
constexpr std::string_view TEST_ENVP0 = "PATH=/bin";
constexpr std::string_view TEST_ENVP1 = "TERM=mnos";

[[nodiscard]] mm::AddressSpace make_address_space(platform::Machine& machine)
{
    return mm::AddressSpace{machine.memory_bus(), TEST_ROOT_TABLE, TEST_NEXT_TABLE, TEST_TABLE_ARENA_END};
}

[[nodiscard]] proc::UserProgram make_user_program()
{
    proc::UserProgram program{TEST_TEXT_BASE};
    program.set_initial_stack_size_bytes(TEST_SMALL_STACK_SIZE_BYTES);
    program.add_segment(proc::UserSegment::text(TEST_TEXT_BASE, std::vector<cpu::Byte>{TEST_TEXT_BYTE_0, TEST_TEXT_BYTE_1}));
    program.add_segment(proc::UserSegment::data(TEST_DATA_BASE, std::vector<cpu::Byte>{TEST_DATA_BYTE_0, TEST_DATA_BYTE_1}));
    return program;
}

[[nodiscard]] cpu::Qword read_user_qword(
    platform::Machine& machine,
    proc::Process& process,
    const mm::VirtualAddress address)
{
    cpu::CpuState cpu_state;
    process.address_space().activate(
        cpu_state,
        cpu_memory::ProcessContextId{static_cast<cpu_memory::ProcessContextId::value_type>(process.id().value())},
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    cpu_memory::MemoryManagementUnit mmu;
    return mmu.read(
        machine.memory_bus(),
        cpu_state.paging(),
        cpu_system::PrivilegeLevel::RING3,
        mm::to_cpu_address(address),
        cpu::DataSize::QWORD);
}

[[nodiscard]] std::string read_user_c_string(
    platform::Machine& machine,
    proc::Process& process,
    const mm::VirtualAddress address)
{
    cpu::CpuState cpu_state;
    process.address_space().activate(
        cpu_state,
        cpu_memory::ProcessContextId{static_cast<cpu_memory::ProcessContextId::value_type>(process.id().value())},
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    cpu_memory::MemoryManagementUnit mmu;
    std::string output;
    for (std::size_t byte_index = std::size_t{0}; byte_index < TEST_STACK_STRING_READ_LIMIT; ++byte_index)
    {
        const cpu::Qword value = mmu.read(
            machine.memory_bus(),
            cpu_state.paging(),
            cpu_system::PrivilegeLevel::RING3,
            mm::to_cpu_address(address + static_cast<mm::AddressValue>(byte_index)),
            cpu::DataSize::BYTE);
        if (value == cpu::Qword{0})
        {
            return output;
        }
        output.push_back(static_cast<char>(value));
    }
    throw std::runtime_error{"test user string is not null terminated"};
}
}

TEST(Stage10UserLoaderTest, ClassifiesUserKernelRangesAndStackBounds)
{
    EXPECT_TRUE(mm::is_user_address(mm::ADDRESS_LAYOUT_USER_LOW_BASE));
    EXPECT_TRUE(mm::is_user_address(mm::ADDRESS_LAYOUT_USER_TEXT_BASE));
    EXPECT_TRUE(mm::is_user_range(mm::ADDRESS_LAYOUT_USER_TEXT_BASE, mm::MM_PAGE_SIZE_BYTES));
    EXPECT_FALSE(mm::is_user_address(mm::ADDRESS_LAYOUT_USER_STACK_TOP));
    EXPECT_FALSE(mm::is_user_range(mm::ADDRESS_LAYOUT_USER_STACK_TOP - mm::AddressValue{1}, mm::AddressValue{2}));

    EXPECT_TRUE(mm::is_kernel_address(mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE));
    EXPECT_TRUE(mm::is_kernel_range(mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE, mm::MM_PAGE_SIZE_BYTES));
    EXPECT_FALSE(mm::is_kernel_address(mm::ADDRESS_LAYOUT_USER_TEXT_BASE));

    EXPECT_THAT(
        mm::user_stack_bottom(TEST_SMALL_STACK_SIZE_BYTES),
        Eq(mm::ADDRESS_LAYOUT_USER_STACK_TOP - TEST_SMALL_STACK_SIZE_BYTES));
    EXPECT_THROW(static_cast<void>(mm::user_stack_bottom(mm::AddressValue{0})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(mm::user_stack_bottom(mm::AddressValue{1})), std::out_of_range);
}

TEST(Stage10UserLoaderTest, ValidatesSegmentsProgramsAndEntryPoint)
{
    proc::UserProgram program{TEST_TEXT_BASE};
    EXPECT_FALSE(program.entry_point_is_executable());
    EXPECT_THROW(program.set_initial_stack_size_bytes(mm::AddressValue{1}), std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(proc::UserProgramArguments{{std::string{"bad\0arg", std::size_t{7}}}}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(proc::UserProgramArguments{{}, {std::string{"bad\0env", std::size_t{7}}}}),
        std::invalid_argument);

    program.add_segment(proc::UserSegment::text(TEST_TEXT_BASE, std::vector<cpu::Byte>{TEST_TEXT_BYTE_0}));
    EXPECT_TRUE(program.entry_point_is_executable());
    EXPECT_THROW(
        program.add_segment(proc::UserSegment::data(TEST_TEXT_BASE, std::vector<cpu::Byte>{TEST_DATA_BYTE_0})),
        std::logic_error);

    EXPECT_THROW(
        static_cast<void>(proc::UserProgram{mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE}),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(proc::UserSegment::bss(
            TEST_DATA_BASE + mm::AddressValue{1},
            mm::MM_PAGE_SIZE_BYTES,
            cpu_memory::PagePermissions::user_read_write_no_execute())),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(proc::UserSegment::bss(
            mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE,
            mm::MM_PAGE_SIZE_BYTES,
            cpu_memory::PagePermissions::user_read_write_no_execute())),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(proc::UserSegment::bss(
            TEST_DATA_BASE,
            mm::MM_PAGE_SIZE_BYTES,
            cpu_memory::PagePermissions::kernel_read_write_no_execute())),
        std::invalid_argument);
}

TEST(Stage10UserLoaderTest, LoadsSegmentsStackAndInitializesRing3ThreadState)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{128}, mm::PageNumber{32}};
    proc::Process process{TEST_PROCESS_ID, make_address_space(machine)};
    proc::UserProgram program = make_user_program();
    proc::UserLoader loader{allocator, machine.memory_bus()};

    const proc::UserProcessImage image = loader.load(program, process);

    EXPECT_THAT(image.entry_point(), Eq(TEST_TEXT_BASE));
    EXPECT_THAT(image.stack_bottom(), Eq(mm::user_stack_bottom(TEST_SMALL_STACK_SIZE_BYTES)));
    EXPECT_LT(image.initial_stack_pointer(), mm::ADDRESS_LAYOUT_USER_STACK_TOP);
    EXPECT_THAT(image.initial_stack_pointer().value() % mm::AddressValue{16}, Eq(mm::AddressValue{0}));
    EXPECT_THAT(image.mapped_page_count(), Eq(std::size_t{4}));
    EXPECT_THAT(read_user_qword(machine, process, image.initial_stack_pointer()), Eq(cpu::Qword{0}));
    EXPECT_THAT(
        read_user_qword(machine, process, image.initial_stack_pointer() + mm::AddressValue{8}),
        Eq(TEST_USER_STACK_NULL_POINTER));
    EXPECT_THAT(
        read_user_qword(machine, process, image.initial_stack_pointer() + mm::AddressValue{16}),
        Eq(TEST_USER_STACK_NULL_POINTER));

    const cpu_memory::PageTranslation text_translation =
        process.address_space().page_translation(TEST_TEXT_BASE, cpu_memory::MemoryAccessKind::EXECUTE, cpu_system::PrivilegeLevel::RING3);
    EXPECT_THAT(text_translation.physical_frame_base(), Eq(mm::to_cpu_address(TEST_FIRST_LOADER_PAGE)));
    EXPECT_FALSE(text_translation.permissions().writable());
    EXPECT_TRUE(text_translation.permissions().user_accessible());
    EXPECT_TRUE(text_translation.permissions().executable());
    EXPECT_THAT(machine.memory_bus().read(text_translation.physical_frame_base(), cpu::DataSize::BYTE), Eq(TEST_TEXT_BYTE_0));
    EXPECT_THAT(
        machine.memory_bus().read(text_translation.physical_frame_base() + cpu::Address64{1}, cpu::DataSize::BYTE),
        Eq(TEST_TEXT_BYTE_1));
    EXPECT_THAT(
        machine.memory_bus().read(text_translation.physical_frame_base() + cpu::Address64{2}, cpu::DataSize::BYTE),
        Eq(cpu::Qword{0}));

    const cpu_memory::PageTranslation data_translation =
        process.address_space().page_translation(TEST_DATA_BASE, cpu_memory::MemoryAccessKind::WRITE, cpu_system::PrivilegeLevel::RING3);
    EXPECT_THAT(data_translation.physical_frame_base(), Eq(mm::to_cpu_address(TEST_FIRST_LOADER_PAGE + mm::MM_PAGE_SIZE_BYTES)));
    EXPECT_TRUE(data_translation.permissions().writable());
    EXPECT_FALSE(data_translation.permissions().executable());

    cpu::CpuState data_state;
    process.address_space().activate(
        data_state,
        cpu_memory::ProcessContextId{static_cast<cpu_memory::ProcessContextId::value_type>(TEST_PROCESS_ID.value())},
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    cpu_memory::MemoryManagementUnit mmu;
    mmu.write(
        machine.memory_bus(),
        data_state.paging(),
        cpu_system::PrivilegeLevel::RING3,
        mm::to_cpu_address(TEST_DATA_BASE),
        cpu::DataSize::QWORD,
        TEST_DATA_QWORD);
    EXPECT_THAT(machine.memory_bus().read(data_translation.physical_frame_base(), cpu::DataSize::QWORD), Eq(TEST_DATA_QWORD));

    sched::ThreadContext thread{sched::ThreadId::first_kernel_thread(), TEST_KERNEL_STACK_BOTTOM};
    loader.initialize_user_thread(image, process, thread);

    EXPECT_THAT(thread.cpu_state().privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_THAT(thread.cpu_state().rip(), Eq(static_cast<cpu::InstructionPointer>(TEST_TEXT_BASE.value())));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RSP), Eq(static_cast<cpu::Qword>(image.initial_stack_pointer().value())));
    EXPECT_TRUE(thread.cpu_state().paging().is_enabled());
    EXPECT_TRUE(thread.cpu_state().paging().process_context_id_enabled());
    EXPECT_THAT(thread.cpu_state().paging().process_context_id().value(), Eq(static_cast<cpu_memory::ProcessContextId::value_type>(TEST_PROCESS_ID.value())));
    EXPECT_THAT(thread.cpu_state().paging().cr3(), Eq(mm::to_cpu_address(TEST_ROOT_TABLE)));
}

TEST(Stage10UserLoaderTest, BuildsInitialUserStackArgumentsAndEnvironment)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{128}, mm::PageNumber{32}};
    proc::Process process{TEST_PROCESS_ID, make_address_space(machine)};
    proc::UserProgram program = make_user_program();
    proc::UserLoader loader{allocator, machine.memory_bus()};
    const proc::UserProgramArguments arguments{
        {std::string{TEST_ARGV0}, std::string{TEST_ARGV1}, std::string{TEST_ARGV2}},
        {std::string{TEST_ENVP0}, std::string{TEST_ENVP1}}};

    const proc::UserProcessImage image = loader.load(program, process, arguments);

    const mm::VirtualAddress stack_pointer = image.initial_stack_pointer();
    EXPECT_THAT(stack_pointer.value() % mm::AddressValue{16}, Eq(mm::AddressValue{0}));
    EXPECT_THAT(read_user_qword(machine, process, stack_pointer), Eq(cpu::Qword{3}));
    const cpu::Qword argv0 = read_user_qword(machine, process, stack_pointer + mm::AddressValue{8});
    const cpu::Qword argv1 = read_user_qword(machine, process, stack_pointer + mm::AddressValue{16});
    const cpu::Qword argv2 = read_user_qword(machine, process, stack_pointer + mm::AddressValue{24});
    const cpu::Qword argv_null = read_user_qword(machine, process, stack_pointer + mm::AddressValue{32});
    const cpu::Qword envp0 = read_user_qword(machine, process, stack_pointer + mm::AddressValue{40});
    const cpu::Qword envp1 = read_user_qword(machine, process, stack_pointer + mm::AddressValue{48});
    const cpu::Qword envp_null = read_user_qword(machine, process, stack_pointer + mm::AddressValue{56});

    EXPECT_THAT(read_user_c_string(machine, process, mm::VirtualAddress{argv0}), Eq(std::string{TEST_ARGV0}));
    EXPECT_THAT(read_user_c_string(machine, process, mm::VirtualAddress{argv1}), Eq(std::string{TEST_ARGV1}));
    EXPECT_THAT(read_user_c_string(machine, process, mm::VirtualAddress{argv2}), Eq(std::string{TEST_ARGV2}));
    EXPECT_THAT(argv_null, Eq(TEST_USER_STACK_NULL_POINTER));
    EXPECT_THAT(read_user_c_string(machine, process, mm::VirtualAddress{envp0}), Eq(std::string{TEST_ENVP0}));
    EXPECT_THAT(read_user_c_string(machine, process, mm::VirtualAddress{envp1}), Eq(std::string{TEST_ENVP1}));
    EXPECT_THAT(envp_null, Eq(TEST_USER_STACK_NULL_POINTER));
}

TEST(Stage10UserLoaderTest, RejectsUserStackArgumentsThatDoNotFit)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{128}, mm::PageNumber{32}};
    proc::Process process{TEST_PROCESS_ID, make_address_space(machine)};
    proc::UserProgram program = make_user_program();
    program.set_initial_stack_size_bytes(mm::MM_PAGE_SIZE_BYTES);
    proc::UserLoader loader{allocator, machine.memory_bus()};
    const std::string oversized_argument(static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES), 'A');

    EXPECT_THROW(
        static_cast<void>(loader.load(program, process, proc::UserProgramArguments{{oversized_argument}})),
        std::length_error);
}

TEST(Stage10UserLoaderTest, RejectsLoadingProgramWithoutExecutableEntry)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    mm::PhysicalPageAllocator allocator{mm::PageNumber{128}, mm::PageNumber{32}};
    proc::Process process{TEST_PROCESS_ID, make_address_space(machine)};
    proc::UserProgram program{TEST_TEXT_BASE};
    program.add_segment(proc::UserSegment::data(TEST_DATA_BASE, std::vector<cpu::Byte>{TEST_DATA_BYTE_0}));
    proc::UserLoader loader{allocator, machine.memory_bus()};

    EXPECT_THROW(static_cast<void>(loader.load(program, process)), std::logic_error);
}
