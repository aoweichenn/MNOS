#pragma once

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/flags/id.hpp>

namespace mnos::cpu
{
class Rflags
{
public:
    [[nodiscard]] bool read(FlagId id) const;
    void write(FlagId id, bool value);
    void clear_status_flags() noexcept;
    void update_zero_sign_from_qword(Qword result) noexcept;
    void update_zero_sign_parity_from_qword(Qword result) noexcept;
    [[nodiscard]] Qword raw_bits() const noexcept;

private:
    Qword raw_bits_ = Qword{0};
};
}
