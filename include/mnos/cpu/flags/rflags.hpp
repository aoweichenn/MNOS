//
// Created by aoweichen on 2026/6/6.
//

#pragma once
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/flags/id.hpp>


namespace mnos
{
class Rflags{
public:
    [[nodiscard]] bool read(FlagId id) const;
    void write(FlagId id, bool value);
    void clear_status_flags() noexcept;
    void update_zero_sign_from_qword(UQWORD64 result) noexcept;
    [[nodiscard]] UQWORD64 raw_bits() const noexcept;

private:
    UQWORD64 _raw_bits = UQWORD64{0};
};
}
