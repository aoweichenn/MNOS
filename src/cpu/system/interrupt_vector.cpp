#include <mnos/cpu/system/interrupt_vector.hpp>

namespace
{
constexpr std::string_view INTERRUPT_VECTOR_DIVIDE_ERROR_NAME = "#DE";
constexpr std::string_view INTERRUPT_VECTOR_BREAKPOINT_NAME = "#BP";
constexpr std::string_view INTERRUPT_VECTOR_INVALID_OPCODE_NAME = "#UD";
constexpr std::string_view INTERRUPT_VECTOR_GENERAL_PROTECTION_NAME = "#GP";
constexpr std::string_view INTERRUPT_VECTOR_PAGE_FAULT_NAME = "#PF";
constexpr std::string_view INTERRUPT_VECTOR_TIMER_NAME = "timer";
constexpr std::string_view INTERRUPT_VECTOR_SYSCALL_COMPAT_NAME = "syscall";
constexpr std::string_view INTERRUPT_VECTOR_TLB_SHOOTDOWN_NAME = "tlb-shootdown";
constexpr std::string_view INTERRUPT_VECTOR_RESCHEDULE_NAME = "reschedule";
constexpr std::string_view INTERRUPT_VECTOR_GENERIC_NAME = "interrupt";
}

namespace mnos::cpu::system
{
std::string_view interrupt_vector_to_name(const InterruptVector vector) noexcept
{
    switch (vector.value())
    {
    case INTERRUPT_VECTOR_DIVIDE_ERROR_VALUE:
        return INTERRUPT_VECTOR_DIVIDE_ERROR_NAME;
    case INTERRUPT_VECTOR_BREAKPOINT_VALUE:
        return INTERRUPT_VECTOR_BREAKPOINT_NAME;
    case INTERRUPT_VECTOR_INVALID_OPCODE_VALUE:
        return INTERRUPT_VECTOR_INVALID_OPCODE_NAME;
    case INTERRUPT_VECTOR_GENERAL_PROTECTION_VALUE:
        return INTERRUPT_VECTOR_GENERAL_PROTECTION_NAME;
    case INTERRUPT_VECTOR_PAGE_FAULT_VALUE:
        return INTERRUPT_VECTOR_PAGE_FAULT_NAME;
    case INTERRUPT_VECTOR_TIMER_VALUE:
        return INTERRUPT_VECTOR_TIMER_NAME;
    case INTERRUPT_VECTOR_SYSCALL_COMPAT_VALUE:
        return INTERRUPT_VECTOR_SYSCALL_COMPAT_NAME;
    case INTERRUPT_VECTOR_TLB_SHOOTDOWN_VALUE:
        return INTERRUPT_VECTOR_TLB_SHOOTDOWN_NAME;
    case INTERRUPT_VECTOR_RESCHEDULE_VALUE:
        return INTERRUPT_VECTOR_RESCHEDULE_NAME;
    default:
        break;
    }

    return INTERRUPT_VECTOR_GENERIC_NAME;
}
}
