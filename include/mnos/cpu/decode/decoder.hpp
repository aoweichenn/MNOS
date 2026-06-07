#pragma once

#include <stdexcept>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/instruction/instruction.hpp>

namespace mnos::cpu
{
class DecodeError final : public std::runtime_error
{
public:
    explicit DecodeError(const char* message);
};

struct DecodedInstruction
{
    Instruction instruction;
    InstructionPointer start_rip;
    InstructionPointer next_rip;
};

class Decoder
{
public:
    [[nodiscard]] DecodedInstruction decode(const ExecutableImage& image, InstructionPointer rip) const;
};
}
