#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace mnos::cpu::system
{
enum class TrapKind : std::uint8_t
{
    EXCEPTION,
    INTERRUPT,
    SOFTWARE_INTERRUPT,
    SYSCALL,
    COUNT,
};

inline constexpr std::size_t TRAP_KIND_COUNT = static_cast<std::size_t>(TrapKind::COUNT);

[[nodiscard]] bool is_trap_kind_valid(TrapKind kind) noexcept;
[[nodiscard]] std::size_t trap_kind_to_index(TrapKind kind) noexcept;
[[nodiscard]] std::string_view trap_kind_to_name(TrapKind kind) noexcept;

class TrapFrame final
{
public:
    TrapFrame(
        TrapKind kind,
        InterruptVector vector,
        InstructionPointer interrupted_rip,
        InstructionPointer return_rip,
        Qword rflags,
        Qword stack_pointer,
        PrivilegeLevel privilege_level,
        std::optional<Qword> error_code = std::nullopt);

    [[nodiscard]] TrapKind kind() const noexcept;
    [[nodiscard]] InterruptVector vector() const noexcept;
    [[nodiscard]] InstructionPointer interrupted_rip() const noexcept;
    [[nodiscard]] InstructionPointer return_rip() const noexcept;
    [[nodiscard]] Qword rflags() const noexcept;
    [[nodiscard]] Qword stack_pointer() const noexcept;
    [[nodiscard]] PrivilegeLevel privilege_level() const noexcept;
    [[nodiscard]] bool has_error_code() const noexcept;
    [[nodiscard]] Qword error_code() const;

private:
    TrapKind kind_;
    InterruptVector vector_;
    InstructionPointer interrupted_rip_;
    InstructionPointer return_rip_;
    Qword rflags_;
    Qword stack_pointer_;
    PrivilegeLevel privilege_level_;
    std::optional<Qword> error_code_;
};
}
