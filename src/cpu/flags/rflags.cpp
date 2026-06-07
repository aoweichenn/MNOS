#include <bit>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/flags/rflags.hpp>

namespace
{
constexpr mnos::cpu::Qword RFLAGS_RAW_EMPTY_BITS = mnos::cpu::Qword{0};
constexpr mnos::cpu::Qword RFLAGS_RAW_ONE_BIT = mnos::cpu::Qword{1};
constexpr mnos::cpu::Qword RFLAGS_LOW_BYTE_MASK = mnos::cpu::Qword{0xFF};
constexpr std::size_t RFLAGS_QWORD_SIGN_BIT_INDEX = mnos::cpu::DATA_SIZE_QWORD_BITS - std::size_t{1};

[[nodiscard]] mnos::cpu::Qword make_flag_mask(const mnos::cpu::FlagId id)
{
    return RFLAGS_RAW_ONE_BIT << mnos::cpu::flag_id_to_bit_index(id);
}

[[nodiscard]] constexpr mnos::cpu::Qword make_qword_sign_mask() noexcept
{
    return RFLAGS_RAW_ONE_BIT << RFLAGS_QWORD_SIGN_BIT_INDEX;
}

[[nodiscard]] bool has_even_low_byte_parity(const mnos::cpu::Qword value) noexcept
{
    const auto bit_count = static_cast<unsigned>(std::popcount(value & RFLAGS_LOW_BYTE_MASK));
    return bit_count % 2U == 0U;
}
}

namespace mnos::cpu
{
bool Rflags::read(const FlagId id) const
{
    const Qword mask = make_flag_mask(id);
    return (this->raw_bits_ & mask) != RFLAGS_RAW_EMPTY_BITS;
}

void Rflags::write(const FlagId id, const bool value)
{
    const Qword mask = make_flag_mask(id);
    if (value)
    {
        // 写 1
        this->raw_bits_ |= mask;
        return;
    }
    // 写 0
    this->raw_bits_ &= ~mask;
}

void Rflags::clear_status_flags() noexcept
{
    this->raw_bits_ = RFLAGS_RAW_EMPTY_BITS;
}

void Rflags::update_zero_sign_from_qword(const Qword result) noexcept
{
    // 从结果中更新对应的 ZF 和 SF
    this->write(FlagId::ZF, result == Qword{0});
    this->write(FlagId::SF, (result & make_qword_sign_mask()) != Qword{0});
}

void Rflags::update_zero_sign_parity_from_qword(const Qword result) noexcept
{
    this->update_zero_sign_from_qword(result);
    this->write(FlagId::PF, has_even_low_byte_parity(result));
}

Qword Rflags::raw_bits() const noexcept
{
    return this->raw_bits_;
}
}
