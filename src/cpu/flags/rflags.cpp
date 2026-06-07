#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/flags/rflags.hpp>

namespace
{
constexpr mnos::UQWORD64 RFLAGS_RAW_EMPTY_BITS = mnos::UQWORD64{0};
constexpr mnos::UQWORD64 RFLAGS_RAW_ONE_BIT = mnos::UQWORD64{1};
constexpr std::size_t RFLAGS_QWORD_SIGN_BIT_INDEX = mnos::DATA_SIZE_QWORD_BITS - std::size_t{1};

[[nodiscard]] mnos::UQWORD64 make_flag_mask(const mnos::FlagId id)
{
    return RFLAGS_RAW_ONE_BIT << mnos::flag_id_to_bit_index(id);
}

[[nodiscard]] constexpr mnos::UQWORD64 make_qword_sign_mask() noexcept
{
    return RFLAGS_RAW_ONE_BIT << RFLAGS_QWORD_SIGN_BIT_INDEX;
}
}

namespace mnos
{
bool Rflags::read(const FlagId id) const
{
    const UQWORD64 mask = make_flag_mask(id);
    return (this->raw_bits_ & mask) != RFLAGS_RAW_EMPTY_BITS;
}

void Rflags::write(const FlagId id, const bool value)
{
    const UQWORD64 mask = make_flag_mask(id);
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

void Rflags::update_zero_sign_from_qword(const UQWORD64 result) noexcept
{
    // 从结果中更新对应的 ZF 和 SF
    this->write(FlagId::ZF, result == UQWORD64{0});
    this->write(FlagId::SF, (result & make_qword_sign_mask()) != UQWORD64{0});
}

UQWORD64 Rflags::raw_bits() const noexcept
{
    return this->raw_bits_;
}
}
