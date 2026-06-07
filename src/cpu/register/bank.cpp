#include <stdexcept>

#include <mnos/cpu/register/bank.hpp>

namespace
{
constexpr const char* REGISTER_BANK_ACCESS_INVALID_ID_MESSAGE = "register bank invalid register id";
}

namespace mnos::cpu
{
UQWORD64 RegisterBank::read(const RegisterId id) const
{
    if (!is_register_id_valid(id))
    {
        throw std::out_of_range{REGISTER_BANK_ACCESS_INVALID_ID_MESSAGE};
    }
    return this->registers_[register_id_to_index(id)];
}

void RegisterBank::write(const RegisterId id, const UQWORD64 value)
{
    if (!is_register_id_valid(id))
    {
        throw std::out_of_range{REGISTER_BANK_ACCESS_INVALID_ID_MESSAGE};
    }
    this->registers_[register_id_to_index(id)] = value;
}
}
