#pragma once

#include <array>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/register/id.hpp>

namespace mnos::cpu
{
inline constexpr std::size_t REGISTER_BANK_REGISTER_COUNT = REGISTER_ID_COUNT;

class RegisterBank
{
public:
    [[nodiscard]] Qword read(RegisterId id) const;
    void write(RegisterId id, Qword value);

private:
    std::array<Qword, REGISTER_BANK_REGISTER_COUNT> registers_{};
};
}
