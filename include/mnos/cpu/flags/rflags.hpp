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
    void update_zero_sign_from_qword(UQWORD64 result) noexcept;
    [[nodiscard]] UQWORD64 raw_bits() const noexcept;

private:
    UQWORD64 raw_bits_ = UQWORD64{0};
};
}
