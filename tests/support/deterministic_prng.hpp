#pragma once

#include <cstdint>

namespace mnos::test
{
class DeterministicPrng
{
public:
    explicit constexpr DeterministicPrng(const std::uint64_t seed) noexcept : state_(seed)
    {
    }

    [[nodiscard]] std::uint64_t next() noexcept
    {
        std::uint64_t value = this->state_;
        value ^= value << DETERMINISTIC_PRNG_LEFT_SHIFT_A;
        value ^= value >> DETERMINISTIC_PRNG_RIGHT_SHIFT_B;
        value ^= value << DETERMINISTIC_PRNG_LEFT_SHIFT_C;
        this->state_ = value;
        return value;
    }

    [[nodiscard]] std::uint64_t next_bounded(const std::uint64_t upper_bound) noexcept
    {
        return this->next() % upper_bound;
    }

private:
    static constexpr std::uint64_t DETERMINISTIC_PRNG_LEFT_SHIFT_A = 13;
    static constexpr std::uint64_t DETERMINISTIC_PRNG_RIGHT_SHIFT_B = 7;
    static constexpr std::uint64_t DETERMINISTIC_PRNG_LEFT_SHIFT_C = 17;

    std::uint64_t state_;
};
}
