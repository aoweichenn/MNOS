#include <array>
#include <stdexcept>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/system/trap_frame.hpp>

namespace
{
constexpr std::string_view TRAP_KIND_INVALID_NAME = "<invalid>";
constexpr const char* TRAP_KIND_INVALID_MESSAGE = "trap kind is invalid";
constexpr const char* TRAP_PRIVILEGE_LEVEL_INVALID_MESSAGE = "trap privilege level is invalid";
constexpr const char* TRAP_ERROR_CODE_NOT_PRESENT_MESSAGE = "trap frame has no error code";

class TrapKindCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::system::TrapKind kind) noexcept
    {
        return NAMES.contains(kind);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::system::TrapKind kind) noexcept
    {
        return NAMES.index(kind);
    }

    [[nodiscard]] static std::string_view name(const mnos::cpu::system::TrapKind kind) noexcept
    {
        return NAMES.name(kind);
    }

private:
    inline static constexpr auto NAMES = mnos::core::make_enum_name_table<mnos::cpu::system::TrapKind>(
        std::array<std::string_view, mnos::cpu::system::TRAP_KIND_COUNT>{
            "EXCEPTION",
            "INTERRUPT",
            "SOFTWARE_INTERRUPT",
            "SYSCALL"},
        TRAP_KIND_INVALID_NAME);
};

void require_trap_kind(const mnos::cpu::system::TrapKind kind)
{
    if (!mnos::cpu::system::is_trap_kind_valid(kind))
    {
        throw std::out_of_range{TRAP_KIND_INVALID_MESSAGE};
    }
}

void require_privilege_level(const mnos::cpu::system::PrivilegeLevel privilege_level)
{
    if (!mnos::cpu::system::is_privilege_level_valid(privilege_level))
    {
        throw std::out_of_range{TRAP_PRIVILEGE_LEVEL_INVALID_MESSAGE};
    }
}
}

namespace mnos::cpu::system
{
bool is_trap_kind_valid(const TrapKind kind) noexcept
{
    return TrapKindCatalog::contains(kind);
}

std::size_t trap_kind_to_index(const TrapKind kind) noexcept
{
    return TrapKindCatalog::index(kind);
}

std::string_view trap_kind_to_name(const TrapKind kind) noexcept
{
    return TrapKindCatalog::name(kind);
}

TrapFrame::TrapFrame(
    const TrapKind kind,
    const InterruptVector vector,
    const InstructionPointer interrupted_rip,
    const InstructionPointer return_rip,
    const Qword rflags,
    const Qword stack_pointer,
    const PrivilegeLevel privilege_level,
    std::optional<Qword> error_code) :
    kind_(kind),
    vector_(vector),
    interrupted_rip_(interrupted_rip),
    return_rip_(return_rip),
    rflags_(rflags),
    stack_pointer_(stack_pointer),
    privilege_level_(privilege_level),
    error_code_(error_code)
{
    require_trap_kind(kind);
    require_privilege_level(privilege_level);
}

TrapKind TrapFrame::kind() const noexcept
{
    return this->kind_;
}

InterruptVector TrapFrame::vector() const noexcept
{
    return this->vector_;
}

InstructionPointer TrapFrame::interrupted_rip() const noexcept
{
    return this->interrupted_rip_;
}

InstructionPointer TrapFrame::return_rip() const noexcept
{
    return this->return_rip_;
}

Qword TrapFrame::rflags() const noexcept
{
    return this->rflags_;
}

Qword TrapFrame::stack_pointer() const noexcept
{
    return this->stack_pointer_;
}

PrivilegeLevel TrapFrame::privilege_level() const noexcept
{
    return this->privilege_level_;
}

bool TrapFrame::has_error_code() const noexcept
{
    return this->error_code_.has_value();
}

Qword TrapFrame::error_code() const
{
    if (!this->error_code_.has_value())
    {
        throw std::logic_error{TRAP_ERROR_CODE_NOT_PRESENT_MESSAGE};
    }
    return this->error_code_.value();
}
}
