#pragma once

#include <array>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/register/id.hpp>

namespace mnos::cpu
{
inline constexpr std::size_t REGISTER_BANK_GENERAL_REGISTER_STORAGE_COUNT = REGISTER_ID_GENERAL_REGISTER_COUNT;

// 寄存器组
class RegisterBank
{
public:
    // 从寄存器中读取数据
    [[nodiscard]] UQWORD64 read(RegisterId id) const;
    // 写入数据到寄存器中
    void write(RegisterId id, UQWORD64 value);

private:
    std::array<UQWORD64, REGISTER_BANK_GENERAL_REGISTER_STORAGE_COUNT> registers_{};
};
}
