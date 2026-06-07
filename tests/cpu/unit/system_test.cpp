#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/core_topology.hpp>
#include <mnos/cpu/system/descriptor_tables.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/cpu/system/trap_controller.hpp>
#include <mnos/cpu/system/trap_frame.hpp>

namespace cpu = mnos::cpu;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_PRIVILEGE_LEVEL =
    static_cast<cpu_system::PrivilegeLevel>(cpu_system::PRIVILEGE_LEVEL_COUNT);
constexpr auto TEST_INVALID_GATE_TYPE = static_cast<cpu_system::GateType>(cpu_system::GATE_TYPE_COUNT);
constexpr auto TEST_INVALID_SEGMENT_KIND =
    static_cast<cpu_system::SegmentDescriptorKind>(cpu_system::SEGMENT_DESCRIPTOR_KIND_COUNT);
constexpr auto TEST_INVALID_TRAP_KIND = static_cast<cpu_system::TrapKind>(cpu_system::TRAP_KIND_COUNT);

constexpr cpu::Address64 TEST_HANDLER_RIP = cpu::Address64{0x1000};
constexpr cpu::Address64 TEST_SYSCALL_RIP = cpu::Address64{0x2000};
constexpr cpu::Address64 TEST_TSS_BASE = cpu::Address64{0x3000};
constexpr cpu::Dword TEST_TSS_LIMIT = cpu::Dword{0x68};
constexpr cpu::Qword TEST_USER_STACK = cpu::Qword{0x8000};
constexpr cpu::Qword TEST_KERNEL_STACK = cpu::Qword{0x9000};
constexpr cpu::Qword TEST_ERROR_CODE = cpu::Qword{0x2};
constexpr std::uint16_t TEST_KERNEL_CODE_SELECTOR_INDEX = 1;
constexpr std::uint16_t TEST_TSS_SELECTOR_INDEX = 5;
constexpr std::uint16_t TEST_OUT_OF_RANGE_SELECTOR_INDEX =
    static_cast<std::uint16_t>(cpu_system::GDT_DESCRIPTOR_COUNT);
constexpr std::uint32_t TEST_CORE_COUNT = std::uint32_t{4};
constexpr cpu_system::CoreId TEST_BOOTSTRAP_CORE = cpu_system::CoreId{0};
constexpr cpu_system::CoreId TEST_LAST_CORE = cpu_system::CoreId{3};
constexpr cpu_system::CoreId TEST_OUT_OF_RANGE_CORE = cpu_system::CoreId{4};
}

TEST(SystemCoreTopologyTest, ModelsCoreIdsAndTopologyBounds)
{
    EXPECT_THAT(cpu_system::CoreId::bootstrap(), Eq(TEST_BOOTSTRAP_CORE));
    EXPECT_TRUE(cpu_system::CoreId{1} != cpu_system::CoreId{2});
    EXPECT_TRUE(cpu_system::CoreId{1} < cpu_system::CoreId{2});

    const cpu_system::CoreTopology default_topology;
    EXPECT_THAT(default_topology.core_count(), Eq(cpu_system::CORE_TOPOLOGY_DEFAULT_CORE_COUNT));
    EXPECT_TRUE(default_topology.contains(cpu_system::CoreId::bootstrap()));

    const cpu_system::CoreTopology single_core = cpu_system::CoreTopology::single_core();
    EXPECT_THAT(single_core.core_count(), Eq(cpu_system::CORE_TOPOLOGY_DEFAULT_CORE_COUNT));
    EXPECT_THAT(single_core.core_at(cpu_system::CORE_ID_BOOTSTRAP_VALUE), Eq(cpu_system::CoreId::bootstrap()));

    const cpu_system::CoreTopology topology{TEST_CORE_COUNT};
    EXPECT_THAT(topology.core_count(), Eq(TEST_CORE_COUNT));
    EXPECT_THAT(topology.bootstrap_core(), Eq(TEST_BOOTSTRAP_CORE));
    EXPECT_THAT(topology.core_at(TEST_LAST_CORE.value()), Eq(TEST_LAST_CORE));
    EXPECT_TRUE(topology.contains(TEST_LAST_CORE));
    EXPECT_FALSE(topology.contains(TEST_OUT_OF_RANGE_CORE));
    EXPECT_THROW(static_cast<void>(topology.core_at(TEST_CORE_COUNT)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(cpu_system::CoreTopology{std::uint32_t{0}}), std::invalid_argument);
}

TEST(SystemPrivilegeTest, MapsPrivilegeLevelsToRingNumbers)
{
    EXPECT_TRUE(cpu_system::is_privilege_level_valid(cpu_system::PrivilegeLevel::RING0));
    EXPECT_THAT(cpu_system::privilege_level_to_ring(cpu_system::PrivilegeLevel::RING3), Eq(cpu_system::RingNumber{3}));
    EXPECT_THAT(
        cpu_system::privilege_level_to_name(cpu_system::PrivilegeLevel::RING0),
        Eq(std::string_view{"RING0"}));
    EXPECT_TRUE(cpu_system::is_more_privileged(
        cpu_system::PrivilegeLevel::RING0,
        cpu_system::PrivilegeLevel::RING3));
    EXPECT_FALSE(cpu_system::is_privilege_level_valid(TEST_INVALID_PRIVILEGE_LEVEL));
    EXPECT_THROW(static_cast<void>(cpu_system::privilege_level_to_ring(TEST_INVALID_PRIVILEGE_LEVEL)), std::out_of_range);
}

TEST(SystemDescriptorTableTest, ModelsInterruptGatesGdtSelectorsAndTssStacks)
{
    EXPECT_TRUE(cpu_system::is_gate_type_valid(cpu_system::GateType::INTERRUPT_GATE));
    EXPECT_FALSE(cpu_system::is_gate_type_valid(TEST_INVALID_GATE_TYPE));
    EXPECT_THAT(cpu_system::gate_type_to_index(cpu_system::GateType::TRAP_GATE), Eq(std::size_t{1}));
    EXPECT_THAT(
        cpu_system::gate_type_to_name(cpu_system::GateType::TRAP_GATE),
        Eq(std::string_view{"TRAP_GATE"}));

    cpu_system::InterruptDescriptorTable idt;
    EXPECT_FALSE(idt.has_gate(cpu_system::InterruptVector::breakpoint()));
    EXPECT_THROW(static_cast<void>(idt.gate_at(cpu_system::InterruptVector::breakpoint())), std::out_of_range);
    idt.set_gate(
        cpu_system::InterruptVector::breakpoint(),
        cpu_system::InterruptGate::interrupt_gate(TEST_HANDLER_RIP, cpu_system::PrivilegeLevel::RING0));
    EXPECT_TRUE(idt.has_gate(cpu_system::InterruptVector::breakpoint()));
    EXPECT_THAT(idt.gate_at(cpu_system::InterruptVector::breakpoint()).handler_rip(), Eq(TEST_HANDLER_RIP));
    idt.clear_gate(cpu_system::InterruptVector::breakpoint());
    EXPECT_FALSE(idt.has_gate(cpu_system::InterruptVector::breakpoint()));

    EXPECT_TRUE(cpu_system::is_segment_descriptor_kind_valid(cpu_system::SegmentDescriptorKind::CODE));
    EXPECT_FALSE(cpu_system::is_segment_descriptor_kind_valid(TEST_INVALID_SEGMENT_KIND));
    EXPECT_THAT(
        cpu_system::segment_descriptor_kind_to_index(cpu_system::SegmentDescriptorKind::DATA),
        Eq(std::size_t{1}));
    EXPECT_THAT(
        cpu_system::segment_descriptor_kind_to_name(cpu_system::SegmentDescriptorKind::TSS),
        Eq(std::string_view{"TSS"}));

    const cpu_system::SegmentSelector null_selector;
    EXPECT_TRUE(null_selector.is_null());
    const cpu_system::SegmentSelector kernel_code =
        cpu_system::SegmentSelector::from_index(TEST_KERNEL_CODE_SELECTOR_INDEX, cpu_system::PrivilegeLevel::RING0);
    EXPECT_THAT(
        kernel_code.value(),
        Eq(static_cast<cpu_system::SegmentSelector::value_type>(TEST_KERNEL_CODE_SELECTOR_INDEX << cpu_system::GDT_SELECTOR_INDEX_SHIFT)));
    EXPECT_THAT(kernel_code.index(), Eq(TEST_KERNEL_CODE_SELECTOR_INDEX));
    EXPECT_THAT(kernel_code.requested_privilege_level(), Eq(cpu_system::PrivilegeLevel::RING0));
    EXPECT_FALSE(kernel_code.is_null());
    EXPECT_THROW(
        static_cast<void>(
            cpu_system::SegmentSelector::from_index(TEST_OUT_OF_RANGE_SELECTOR_INDEX, cpu_system::PrivilegeLevel::RING0)),
        std::out_of_range);

    cpu_system::GlobalDescriptorTable gdt;
    EXPECT_FALSE(gdt.has_descriptor(kernel_code));
    EXPECT_THROW(static_cast<void>(gdt.descriptor_at(kernel_code)), std::out_of_range);
    EXPECT_FALSE(cpu_system::SegmentDescriptor::absent().is_present());
    gdt.set_descriptor(kernel_code, cpu_system::SegmentDescriptor::code(cpu_system::PrivilegeLevel::RING0));
    EXPECT_TRUE(gdt.has_descriptor(kernel_code));
    EXPECT_THAT(gdt.descriptor_at(kernel_code).kind(), Eq(cpu_system::SegmentDescriptorKind::CODE));
    EXPECT_THAT(gdt.descriptor_at(kernel_code).descriptor_privilege_level(), Eq(cpu_system::PrivilegeLevel::RING0));
    gdt.clear_descriptor(kernel_code);
    EXPECT_FALSE(gdt.has_descriptor(kernel_code));
    gdt.set_descriptor(kernel_code, cpu_system::SegmentDescriptor::data(cpu_system::PrivilegeLevel::RING3));
    EXPECT_THAT(gdt.descriptor_at(kernel_code).kind(), Eq(cpu_system::SegmentDescriptorKind::DATA));
    EXPECT_THAT(gdt.descriptor_at(kernel_code).descriptor_privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));

    const cpu_system::SegmentSelector tss_selector =
        cpu_system::SegmentSelector::from_index(TEST_TSS_SELECTOR_INDEX, cpu_system::PrivilegeLevel::RING0);
    gdt.set_descriptor(tss_selector, cpu_system::SegmentDescriptor::tss(TEST_TSS_BASE, TEST_TSS_LIMIT));
    EXPECT_THAT(gdt.descriptor_at(tss_selector).base_address(), Eq(TEST_TSS_BASE));
    EXPECT_THAT(gdt.descriptor_at(tss_selector).limit_bytes(), Eq(TEST_TSS_LIMIT));

    cpu_system::TaskStateSegment tss;
    EXPECT_FALSE(tss.has_privilege_stack(cpu_system::PrivilegeLevel::RING0));
    tss.set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_KERNEL_STACK);
    EXPECT_TRUE(tss.has_privilege_stack(cpu_system::PrivilegeLevel::RING0));
    EXPECT_THAT(tss.privilege_stack(cpu_system::PrivilegeLevel::RING0), Eq(TEST_KERNEL_STACK));
    tss.clear_privilege_stack(cpu_system::PrivilegeLevel::RING0);
    EXPECT_FALSE(tss.has_privilege_stack(cpu_system::PrivilegeLevel::RING0));
    EXPECT_THROW(static_cast<void>(tss.privilege_stack(cpu_system::PrivilegeLevel::RING0)), std::out_of_range);
    EXPECT_THROW(tss.set_privilege_stack(cpu_system::PrivilegeLevel::RING3, TEST_KERNEL_STACK), std::out_of_range);
}

TEST(SystemTrapFrameTest, StoresTrapMetadataAndOptionalErrorCode)
{
    EXPECT_TRUE(cpu_system::is_trap_kind_valid(cpu_system::TrapKind::EXCEPTION));
    EXPECT_FALSE(cpu_system::is_trap_kind_valid(TEST_INVALID_TRAP_KIND));
    EXPECT_THAT(cpu_system::trap_kind_to_index(cpu_system::TrapKind::SYSCALL), Eq(std::size_t{3}));
    EXPECT_THAT(
        cpu_system::trap_kind_to_name(cpu_system::TrapKind::SYSCALL),
        Eq(std::string_view{"SYSCALL"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector::divide_error()),
        Eq(std::string_view{"#DE"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector::breakpoint()),
        Eq(std::string_view{"#BP"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector::invalid_opcode()),
        Eq(std::string_view{"#UD"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector::general_protection()),
        Eq(std::string_view{"#GP"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector::page_fault()),
        Eq(std::string_view{"#PF"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector::timer()),
        Eq(std::string_view{"timer"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector::syscall_compat()),
        Eq(std::string_view{"syscall"}));
    EXPECT_THAT(
        cpu_system::interrupt_vector_to_name(cpu_system::InterruptVector{0x55}),
        Eq(std::string_view{"interrupt"}));

    const cpu_system::TrapFrame frame{
        cpu_system::TrapKind::EXCEPTION,
        cpu_system::InterruptVector::page_fault(),
        cpu::InstructionPointer{4},
        cpu::InstructionPointer{4},
        cpu::Qword{0},
        TEST_USER_STACK,
        cpu_system::PrivilegeLevel::RING3,
        TEST_ERROR_CODE};

    EXPECT_THAT(frame.kind(), Eq(cpu_system::TrapKind::EXCEPTION));
    EXPECT_THAT(frame.vector(), Eq(cpu_system::InterruptVector::page_fault()));
    EXPECT_THAT(frame.interrupted_rip(), Eq(cpu::InstructionPointer{4}));
    EXPECT_TRUE(frame.has_error_code());
    EXPECT_THAT(frame.error_code(), Eq(TEST_ERROR_CODE));

    const cpu_system::TrapFrame no_error_frame{
        cpu_system::TrapKind::INTERRUPT,
        cpu_system::InterruptVector::timer(),
        cpu::InstructionPointer{8},
        cpu::InstructionPointer{8},
        cpu::Qword{0},
        TEST_KERNEL_STACK,
        cpu_system::PrivilegeLevel::RING0};
    EXPECT_FALSE(no_error_frame.has_error_code());
    EXPECT_THROW(static_cast<void>(no_error_frame.error_code()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(cpu_system::TrapFrame{
            TEST_INVALID_TRAP_KIND,
            cpu_system::InterruptVector::timer(),
            cpu::InstructionPointer{0},
            cpu::InstructionPointer{0},
            cpu::Qword{0},
            TEST_KERNEL_STACK,
            cpu_system::PrivilegeLevel::RING0}),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(cpu_system::TrapFrame{
            cpu_system::TrapKind::INTERRUPT,
            cpu_system::InterruptVector::timer(),
            cpu::InstructionPointer{0},
            cpu::InstructionPointer{0},
            cpu::Qword{0},
            TEST_KERNEL_STACK,
            TEST_INVALID_PRIVILEGE_LEVEL}),
        std::out_of_range);
}

TEST(SystemTrapControllerTest, DispatchesInterruptGatesAndRestoresTrapFrames)
{
    cpu_system::TrapController controller;
    controller.idt().set_gate(
        cpu_system::InterruptVector::breakpoint(),
        cpu_system::InterruptGate::interrupt_gate(TEST_HANDLER_RIP, cpu_system::PrivilegeLevel::RING0));
    controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_KERNEL_STACK);

    cpu::CpuState state;
    state.set_rip(cpu::InstructionPointer{5});
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.registers().write(cpu::RegisterId::RSP, TEST_USER_STACK);
    state.flags().write(cpu::FlagId::IF, true);

    const cpu_system::TrapFrame frame =
        controller.raise_software_interrupt(state, cpu_system::InterruptVector::breakpoint(), cpu::InstructionPointer{6});

    EXPECT_THAT(frame.return_rip(), Eq(cpu::InstructionPointer{6}));
    EXPECT_THAT(frame.stack_pointer(), Eq(TEST_USER_STACK));
    EXPECT_THAT(state.rip(), Eq(TEST_HANDLER_RIP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_KERNEL_STACK));
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING0));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::IF));
    EXPECT_TRUE(state.has_pending_trap());

    controller.return_from_trap(state);
    EXPECT_THAT(state.rip(), Eq(cpu::InstructionPointer{6}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_USER_STACK));
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::IF));
    EXPECT_FALSE(state.has_pending_trap());
}

TEST(SystemTrapControllerTest, EntersAndReturnsFromSyscall)
{
    cpu_system::TrapController controller;
    controller.configure_syscall(cpu_system::SyscallDescriptor::enabled(TEST_SYSCALL_RIP));
    controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_KERNEL_STACK);

    cpu::CpuState state;
    state.set_rip(cpu::InstructionPointer{10});
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.registers().write(cpu::RegisterId::RSP, TEST_USER_STACK);
    state.flags().write(cpu::FlagId::IF, true);

    const cpu_system::TrapFrame frame = controller.enter_syscall(state, cpu::InstructionPointer{12});

    EXPECT_THAT(frame.kind(), Eq(cpu_system::TrapKind::SYSCALL));
    EXPECT_THAT(state.rip(), Eq(TEST_SYSCALL_RIP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RCX), Eq(cpu::Qword{12}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::R11) & cpu_system::SYSCALL_DEFAULT_RFLAGS_MASK, Eq(cpu_system::SYSCALL_DEFAULT_RFLAGS_MASK));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::IF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_KERNEL_STACK));

    controller.return_from_syscall(state);
    EXPECT_THAT(state.rip(), Eq(cpu::InstructionPointer{12}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_USER_STACK));
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::IF));
    EXPECT_FALSE(state.has_pending_trap());
}

TEST(SystemTrapControllerTest, LoadsTablesAndRejectsInvalidTrapTransitions)
{
    cpu_system::GlobalDescriptorTable gdt;
    const cpu_system::SegmentSelector kernel_code =
        cpu_system::SegmentSelector::from_index(TEST_KERNEL_CODE_SELECTOR_INDEX, cpu_system::PrivilegeLevel::RING0);
    gdt.set_descriptor(kernel_code, cpu_system::SegmentDescriptor::code(cpu_system::PrivilegeLevel::RING0));

    cpu_system::TaskStateSegment tss;
    tss.set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_KERNEL_STACK);

    cpu_system::InterruptDescriptorTable idt;
    idt.set_gate(
        cpu_system::InterruptVector::timer(),
        cpu_system::InterruptGate::trap_gate(TEST_HANDLER_RIP, cpu_system::PrivilegeLevel::RING3));
    idt.set_gate(
        cpu_system::InterruptVector::invalid_opcode(),
        cpu_system::InterruptGate::interrupt_gate(TEST_HANDLER_RIP, cpu_system::PrivilegeLevel::RING0));

    cpu_system::TrapController controller;
    controller.load_gdt(gdt);
    controller.load_task_state_segment(tss);
    controller.load_idt(idt);
    controller.configure_syscall(cpu_system::SyscallDescriptor::enabled(TEST_SYSCALL_RIP));

    const cpu_system::TrapController& const_controller = controller;
    EXPECT_TRUE(const_controller.gdt().has_descriptor(kernel_code));
    EXPECT_TRUE(const_controller.tss().has_privilege_stack(cpu_system::PrivilegeLevel::RING0));
    EXPECT_TRUE(const_controller.idt().has_gate(cpu_system::InterruptVector::timer()));
    EXPECT_TRUE(const_controller.syscall_descriptor().is_enabled());

    cpu::CpuState same_ring_state;
    same_ring_state.set_rip(cpu::InstructionPointer{5});
    same_ring_state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    same_ring_state.registers().write(cpu::RegisterId::RSP, TEST_USER_STACK);
    same_ring_state.flags().write(cpu::FlagId::IF, true);
    const cpu_system::TrapFrame interrupt_frame =
        controller.raise_interrupt(same_ring_state, cpu_system::InterruptVector::timer());
    EXPECT_THAT(interrupt_frame.kind(), Eq(cpu_system::TrapKind::INTERRUPT));
    EXPECT_THAT(same_ring_state.rip(), Eq(TEST_HANDLER_RIP));
    EXPECT_THAT(same_ring_state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(same_ring_state.flags().read(cpu::FlagId::IF));

    cpu::CpuState exception_state;
    exception_state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    exception_state.registers().write(cpu::RegisterId::RSP, TEST_USER_STACK);
    const cpu_system::TrapFrame exception_frame =
        controller.raise_exception(exception_state, cpu_system::InterruptVector::invalid_opcode(), TEST_ERROR_CODE);
    EXPECT_THAT(exception_frame.kind(), Eq(cpu_system::TrapKind::EXCEPTION));
    EXPECT_TRUE(exception_frame.has_error_code());
    EXPECT_THAT(exception_state.registers().read(cpu::RegisterId::RSP), Eq(TEST_KERNEL_STACK));

    cpu_system::TrapController missing_tss_controller;
    missing_tss_controller.idt().set_gate(
        cpu_system::InterruptVector::breakpoint(),
        cpu_system::InterruptGate::interrupt_gate(TEST_HANDLER_RIP, cpu_system::PrivilegeLevel::RING0));
    cpu::CpuState missing_tss_state;
    missing_tss_state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    EXPECT_THROW(
        static_cast<void>(missing_tss_controller.raise_interrupt(
            missing_tss_state,
            cpu_system::InterruptVector::breakpoint())),
        std::logic_error);
    EXPECT_FALSE(missing_tss_state.has_pending_trap());

    cpu::CpuState no_pending_state;
    EXPECT_THROW(controller.return_from_trap(no_pending_state), std::logic_error);
    EXPECT_THROW(controller.return_from_syscall(no_pending_state), std::logic_error);
    no_pending_state.set_pending_trap(cpu_system::TrapFrame{
        cpu_system::TrapKind::INTERRUPT,
        cpu_system::InterruptVector::timer(),
        cpu::InstructionPointer{1},
        cpu::InstructionPointer{1},
        cpu::Qword{0},
        TEST_USER_STACK,
        cpu_system::PrivilegeLevel::RING0});
    EXPECT_THROW(controller.return_from_syscall(no_pending_state), std::logic_error);
}
