#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <mnos/cpu/decode/decoder.hpp>

namespace
{
constexpr const char* DECODER_EMPTY_IMAGE_MESSAGE = "decoder cannot decode an empty executable image";
constexpr const char* DECODER_TRUNCATED_INSTRUCTION_MESSAGE = "decoder reached the end of image inside an instruction";
constexpr const char* DECODER_UNSUPPORTED_OPCODE_MESSAGE = "decoder unsupported x86-64 opcode";
constexpr const char* DECODER_REX_W_REQUIRED_MESSAGE = "decoder instruction requires REX.W";
constexpr const char* DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE = "decoder unsupported ModRM opcode extension";
constexpr std::uint8_t X86_REX_PREFIX_MIN = 0x40;
constexpr std::uint8_t X86_REX_PREFIX_MAX = 0x4F;
constexpr std::uint8_t X86_REX_W_MASK = 0x08;
constexpr std::uint8_t X86_REX_R_MASK = 0x04;
constexpr std::uint8_t X86_REX_X_MASK = 0x02;
constexpr std::uint8_t X86_REX_B_MASK = 0x01;
constexpr std::uint8_t X86_REX_EXTENDED_REGISTER_OFFSET = 8;
constexpr std::uint8_t X86_OPCODE_ESCAPE = 0x0F;
constexpr std::uint8_t X86_OPCODE_HLT = 0xF4;
constexpr std::uint8_t X86_OPCODE_JMP_REL8 = 0xEB;
constexpr std::uint8_t X86_OPCODE_JMP_REL32 = 0xE9;
constexpr std::uint8_t X86_OPCODE_JE_REL8 = 0x74;
constexpr std::uint8_t X86_OPCODE_JNE_REL8 = 0x75;
constexpr std::uint8_t X86_OPCODE_JE_REL32 = 0x84;
constexpr std::uint8_t X86_OPCODE_JNE_REL32 = 0x85;
constexpr std::uint8_t X86_OPCODE_MOV_R64_IMM64_MIN = 0xB8;
constexpr std::uint8_t X86_OPCODE_MOV_R64_IMM64_MAX = 0xBF;
constexpr std::uint8_t X86_OPCODE_MOV_RM64_R64 = 0x89;
constexpr std::uint8_t X86_OPCODE_MOV_R64_RM64 = 0x8B;
constexpr std::uint8_t X86_OPCODE_ADD_RM64_R64 = 0x01;
constexpr std::uint8_t X86_OPCODE_ADD_R64_RM64 = 0x03;
constexpr std::uint8_t X86_OPCODE_SUB_RM64_R64 = 0x29;
constexpr std::uint8_t X86_OPCODE_SUB_R64_RM64 = 0x2B;
constexpr std::uint8_t X86_OPCODE_CMP_RM64_R64 = 0x39;
constexpr std::uint8_t X86_OPCODE_CMP_R64_RM64 = 0x3B;
constexpr std::uint8_t X86_OPCODE_GROUP1_RM64_IMM32 = 0x81;
constexpr std::uint8_t X86_MODRM_MOD_SHIFT = 6;
constexpr std::uint8_t X86_MODRM_REG_SHIFT = 3;
constexpr std::uint8_t X86_MODRM_FIELD_MASK = 0b111;
constexpr std::uint8_t X86_MODRM_MOD_MASK = 0b11;
constexpr std::uint8_t X86_MODRM_MOD_NO_DISPLACEMENT = 0b00;
constexpr std::uint8_t X86_MODRM_MOD_REGISTER = 0b11;
constexpr std::uint8_t X86_MODRM_MOD_DISP8 = 0b01;
constexpr std::uint8_t X86_MODRM_MOD_DISP32 = 0b10;
constexpr std::uint8_t X86_MODRM_RM_SIB = 0b100;
constexpr std::uint8_t X86_MODRM_RM_RIP_RELATIVE = 0b101;
constexpr std::uint8_t X86_SIB_SCALE_SHIFT = 6;
constexpr std::uint8_t X86_SIB_INDEX_SHIFT = 3;
constexpr std::uint8_t X86_SIB_FIELD_MASK = 0b111;
constexpr std::uint8_t X86_SIB_INDEX_NONE = 0b100;
constexpr std::uint8_t X86_SIB_BASE_NONE = 0b101;
constexpr std::uint8_t X86_GROUP1_ADD_EXTENSION = 0;
constexpr std::uint8_t X86_GROUP1_SUB_EXTENSION = 5;
constexpr std::uint8_t X86_GROUP1_CMP_EXTENSION = 7;
constexpr std::size_t X86_REGISTER_COUNT = 16;

struct RexPrefix
{
    bool w = false;
    bool r = false;
    bool x = false;
    bool b = false;
};

struct ModRmByte
{
    std::uint8_t mod = 0;
    std::uint8_t reg = 0;
    std::uint8_t rm = 0;
};

struct SibByte
{
    std::uint8_t scale = 0;
    std::uint8_t index = 0;
    std::uint8_t base = 0;
};

class DecodeCursor
{
public:
    DecodeCursor(const mnos::cpu::ExecutableImage& image, const mnos::cpu::InstructionPointer rip) :
        image_(image), rip_(rip)
    {
        if (image.empty())
        {
            throw mnos::cpu::DecodeError{DECODER_EMPTY_IMAGE_MESSAGE};
        }
    }

    [[nodiscard]] mnos::cpu::InstructionPointer rip() const noexcept
    {
        return this->rip_;
    }

    [[nodiscard]] std::uint8_t peek_u8() const
    {
        if (!this->image_.contains_rip(this->rip_))
        {
            throw mnos::cpu::DecodeError{DECODER_TRUNCATED_INSTRUCTION_MESSAGE};
        }
        return this->image_.byte_at(this->rip_);
    }

    [[nodiscard]] std::uint8_t read_u8()
    {
        if (!this->image_.contains_rip(this->rip_))
        {
            throw mnos::cpu::DecodeError{DECODER_TRUNCATED_INSTRUCTION_MESSAGE};
        }

        const std::uint8_t value = this->image_.byte_at(this->rip_);
        ++this->rip_;
        return value;
    }

    [[nodiscard]] std::int8_t read_i8()
    {
        return static_cast<std::int8_t>(this->read_u8());
    }

    [[nodiscard]] std::uint32_t read_u32()
    {
        std::uint32_t value = 0;
        for (std::size_t byte_index = 0; byte_index < mnos::cpu::DATA_SIZE_DWORD_BYTES; ++byte_index)
        {
            value |= static_cast<std::uint32_t>(this->read_u8()) << (byte_index * mnos::cpu::DATA_SIZE_BYTE_BITS);
        }
        return value;
    }

    [[nodiscard]] std::int32_t read_i32()
    {
        return static_cast<std::int32_t>(this->read_u32());
    }

    [[nodiscard]] std::uint64_t read_u64()
    {
        std::uint64_t value = 0;
        for (std::size_t byte_index = 0; byte_index < mnos::cpu::DATA_SIZE_QWORD_BYTES; ++byte_index)
        {
            value |= static_cast<std::uint64_t>(this->read_u8()) << (byte_index * mnos::cpu::DATA_SIZE_BYTE_BITS);
        }
        return value;
    }

private:
    const mnos::cpu::ExecutableImage& image_;
    mnos::cpu::InstructionPointer rip_;
};

[[nodiscard]] bool is_rex_prefix(const std::uint8_t value) noexcept
{
    return value >= X86_REX_PREFIX_MIN && value <= X86_REX_PREFIX_MAX;
}

[[nodiscard]] RexPrefix read_rex_prefixes(DecodeCursor& cursor)
{
    RexPrefix rex;
    while (is_rex_prefix(cursor.peek_u8()))
    {
        const std::uint8_t value = cursor.read_u8();
        rex.w = (value & X86_REX_W_MASK) != 0;
        rex.r = (value & X86_REX_R_MASK) != 0;
        rex.x = (value & X86_REX_X_MASK) != 0;
        rex.b = (value & X86_REX_B_MASK) != 0;
    }
    return rex;
}

void require_rex_w(const RexPrefix& rex)
{
    if (!rex.w)
    {
        throw mnos::cpu::DecodeError{DECODER_REX_W_REQUIRED_MESSAGE};
    }
}

[[nodiscard]] ModRmByte read_modrm(DecodeCursor& cursor)
{
    const std::uint8_t value = cursor.read_u8();
    return ModRmByte{
        static_cast<std::uint8_t>((value >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_MASK),
        static_cast<std::uint8_t>((value >> X86_MODRM_REG_SHIFT) & X86_MODRM_FIELD_MASK),
        static_cast<std::uint8_t>(value & X86_MODRM_FIELD_MASK)};
}

[[nodiscard]] SibByte read_sib(DecodeCursor& cursor)
{
    const std::uint8_t value = cursor.read_u8();
    return SibByte{
        static_cast<std::uint8_t>((value >> X86_SIB_SCALE_SHIFT) & X86_MODRM_MOD_MASK),
        static_cast<std::uint8_t>((value >> X86_SIB_INDEX_SHIFT) & X86_SIB_FIELD_MASK),
        static_cast<std::uint8_t>(value & X86_SIB_FIELD_MASK)};
}

[[nodiscard]] std::uint8_t sib_scale_to_multiplier(const std::uint8_t scale_bits) noexcept
{
    return static_cast<std::uint8_t>(mnos::cpu::MEMORY_ADDRESS_SCALE_1 << scale_bits);
}

[[nodiscard]] mnos::cpu::RegisterId register_from_index(const std::uint8_t index)
{
    static constexpr std::array<mnos::cpu::RegisterId, X86_REGISTER_COUNT> REGISTER_IDS{
        mnos::cpu::RegisterId::RAX,
        mnos::cpu::RegisterId::RCX,
        mnos::cpu::RegisterId::RDX,
        mnos::cpu::RegisterId::RBX,
        mnos::cpu::RegisterId::RSP,
        mnos::cpu::RegisterId::RBP,
        mnos::cpu::RegisterId::RSI,
        mnos::cpu::RegisterId::RDI,
        mnos::cpu::RegisterId::R8,
        mnos::cpu::RegisterId::R9,
        mnos::cpu::RegisterId::R10,
        mnos::cpu::RegisterId::R11,
        mnos::cpu::RegisterId::R12,
        mnos::cpu::RegisterId::R13,
        mnos::cpu::RegisterId::R14,
        mnos::cpu::RegisterId::R15};
    return REGISTER_IDS[index];
}

[[nodiscard]] mnos::cpu::RegisterId decode_register(const std::uint8_t encoded, const bool high_bit)
{
    const std::uint8_t index =
        static_cast<std::uint8_t>(encoded + (high_bit ? X86_REX_EXTENDED_REGISTER_OFFSET : 0U));
    return register_from_index(index);
}

[[nodiscard]] mnos::cpu::SignedQword qword_to_signed_immediate(const std::uint64_t value) noexcept
{
    return std::bit_cast<mnos::cpu::SignedQword>(value);
}

[[nodiscard]] mnos::cpu::SignedQword read_modrm_displacement(DecodeCursor& cursor, const ModRmByte& modrm)
{
    if (modrm.mod == X86_MODRM_MOD_DISP8)
    {
        return static_cast<mnos::cpu::SignedQword>(cursor.read_i8());
    }

    if (modrm.mod == X86_MODRM_MOD_DISP32)
    {
        return static_cast<mnos::cpu::SignedQword>(cursor.read_i32());
    }

    return mnos::cpu::SignedQword{0};
}

[[nodiscard]] mnos::cpu::Operand make_memory_operand_from_sib(
    DecodeCursor& cursor,
    const ModRmByte& modrm,
    const RexPrefix& rex)
{
    const SibByte sib = read_sib(cursor);
    const bool has_index = sib.index != X86_SIB_INDEX_NONE || rex.x;
    const bool has_base = !(modrm.mod == X86_MODRM_MOD_NO_DISPLACEMENT && sib.base == X86_SIB_BASE_NONE);
    mnos::cpu::SignedQword displacement = read_modrm_displacement(cursor, modrm);
    if (!has_base)
    {
        displacement = static_cast<mnos::cpu::SignedQword>(cursor.read_i32());
    }

    if (has_base && has_index)
    {
        return mnos::cpu::Operand::indexed_mem(
            decode_register(sib.base, rex.b),
            decode_register(sib.index, rex.x),
            sib_scale_to_multiplier(sib.scale),
            displacement,
            mnos::cpu::DataSize::QWORD);
    }

    if (has_base)
    {
        return mnos::cpu::Operand::mem(decode_register(sib.base, rex.b), displacement, mnos::cpu::DataSize::QWORD);
    }

    if (has_index)
    {
        return mnos::cpu::Operand::mem(
            mnos::cpu::MemoryAddress::index_displacement(
                decode_register(sib.index, rex.x),
                sib_scale_to_multiplier(sib.scale),
                displacement),
            mnos::cpu::DataSize::QWORD);
    }

    return mnos::cpu::Operand::absolute_mem(static_cast<mnos::cpu::Address64>(displacement), mnos::cpu::DataSize::QWORD);
}

[[nodiscard]] mnos::cpu::Operand decode_rm64_operand(DecodeCursor& cursor, const ModRmByte& modrm, const RexPrefix& rex)
{
    if (modrm.mod == X86_MODRM_MOD_REGISTER)
    {
        return mnos::cpu::Operand::reg(decode_register(modrm.rm, rex.b));
    }

    if (modrm.rm == X86_MODRM_RM_SIB)
    {
        return make_memory_operand_from_sib(cursor, modrm, rex);
    }

    if (modrm.mod == X86_MODRM_MOD_NO_DISPLACEMENT && modrm.rm == X86_MODRM_RM_RIP_RELATIVE)
    {
        const mnos::cpu::SignedQword displacement = static_cast<mnos::cpu::SignedQword>(cursor.read_i32());
        const mnos::cpu::Address64 address =
            cursor.rip() + static_cast<mnos::cpu::Address64>(displacement);
        return mnos::cpu::Operand::absolute_mem(address, mnos::cpu::DataSize::QWORD);
    }

    return mnos::cpu::Operand::mem(
        decode_register(modrm.rm, rex.b),
        read_modrm_displacement(cursor, modrm),
        mnos::cpu::DataSize::QWORD);
}

[[nodiscard]] mnos::cpu::InstructionPointer relative_target(
    const mnos::cpu::InstructionPointer next_rip,
    const mnos::cpu::SignedQword displacement) noexcept
{
    return next_rip + static_cast<mnos::cpu::InstructionPointer>(displacement);
}

[[nodiscard]] mnos::cpu::Instruction make_alu_instruction(
    const mnos::cpu::Opcode opcode,
    mnos::cpu::Operand destination,
    mnos::cpu::Operand source)
{
    switch (opcode)
    {
    case mnos::cpu::Opcode::ADD:
        return mnos::cpu::Instruction::make_add(std::move(destination), std::move(source));
    case mnos::cpu::Opcode::SUB:
        return mnos::cpu::Instruction::make_sub(std::move(destination), std::move(source));
    case mnos::cpu::Opcode::CMP:
        return mnos::cpu::Instruction::make_cmp(std::move(destination), std::move(source));
    case mnos::cpu::Opcode::MOV:
    case mnos::cpu::Opcode::JMP:
    case mnos::cpu::Opcode::JE:
    case mnos::cpu::Opcode::JNE:
    case mnos::cpu::Opcode::HLT:
    case mnos::cpu::Opcode::COUNT:
        throw mnos::cpu::DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
    }

    throw mnos::cpu::DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
}

[[nodiscard]] mnos::cpu::Instruction make_jump_instruction(
    const mnos::cpu::Opcode opcode,
    const mnos::cpu::InstructionPointer target)
{
    const auto immediate = static_cast<mnos::cpu::SignedQword>(target);
    switch (opcode)
    {
    case mnos::cpu::Opcode::JMP:
        return mnos::cpu::Instruction::make_jmp(mnos::cpu::Operand::imm(immediate));
    case mnos::cpu::Opcode::JE:
        return mnos::cpu::Instruction::make_je(mnos::cpu::Operand::imm(immediate));
    case mnos::cpu::Opcode::JNE:
        return mnos::cpu::Instruction::make_jne(mnos::cpu::Operand::imm(immediate));
    case mnos::cpu::Opcode::MOV:
    case mnos::cpu::Opcode::ADD:
    case mnos::cpu::Opcode::SUB:
    case mnos::cpu::Opcode::CMP:
    case mnos::cpu::Opcode::HLT:
    case mnos::cpu::Opcode::COUNT:
        throw mnos::cpu::DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
    }

    throw mnos::cpu::DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
}
}

namespace mnos::cpu
{
DecodeError::DecodeError(const char* const message) : std::runtime_error(message)
{
}

DecodedInstruction Decoder::decode(const ExecutableImage& image, const InstructionPointer rip) const
{
    DecodeCursor cursor{image, rip};
    const RexPrefix rex = read_rex_prefixes(cursor);
    const std::uint8_t opcode = cursor.read_u8();
    Instruction instruction = Instruction::make_hlt();

    if (opcode == X86_OPCODE_HLT)
    {
        instruction = Instruction::make_hlt();
    }
    else if (opcode >= X86_OPCODE_MOV_R64_IMM64_MIN && opcode <= X86_OPCODE_MOV_R64_IMM64_MAX)
    {
        require_rex_w(rex);
        const RegisterId destination = decode_register(opcode - X86_OPCODE_MOV_R64_IMM64_MIN, rex.b);
        instruction = Instruction::make_mov(
            Operand::reg(destination),
            Operand::imm(qword_to_signed_immediate(cursor.read_u64())));
    }
    else if (opcode == X86_OPCODE_MOV_RM64_R64 || opcode == X86_OPCODE_MOV_R64_RM64 ||
             opcode == X86_OPCODE_ADD_RM64_R64 || opcode == X86_OPCODE_ADD_R64_RM64 ||
             opcode == X86_OPCODE_SUB_RM64_R64 || opcode == X86_OPCODE_SUB_R64_RM64 ||
             opcode == X86_OPCODE_CMP_RM64_R64 || opcode == X86_OPCODE_CMP_R64_RM64)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        Operand rm_operand = decode_rm64_operand(cursor, modrm, rex);
        Operand reg_operand = Operand::reg(decode_register(modrm.reg, rex.r));

        if (opcode == X86_OPCODE_MOV_RM64_R64)
        {
            instruction = Instruction::make_mov(std::move(rm_operand), std::move(reg_operand));
        }
        else if (opcode == X86_OPCODE_MOV_R64_RM64)
        {
            instruction = Instruction::make_mov(std::move(reg_operand), std::move(rm_operand));
        }
        else if (opcode == X86_OPCODE_ADD_RM64_R64 || opcode == X86_OPCODE_SUB_RM64_R64 ||
                 opcode == X86_OPCODE_CMP_RM64_R64)
        {
            const Opcode decoded_opcode = opcode == X86_OPCODE_ADD_RM64_R64
                ? Opcode::ADD
                : (opcode == X86_OPCODE_SUB_RM64_R64 ? Opcode::SUB : Opcode::CMP);
            instruction = make_alu_instruction(decoded_opcode, std::move(rm_operand), std::move(reg_operand));
        }
        else
        {
            const Opcode decoded_opcode = opcode == X86_OPCODE_ADD_R64_RM64
                ? Opcode::ADD
                : (opcode == X86_OPCODE_SUB_R64_RM64 ? Opcode::SUB : Opcode::CMP);
            instruction = make_alu_instruction(decoded_opcode, std::move(reg_operand), std::move(rm_operand));
        }
    }
    else if (opcode == X86_OPCODE_GROUP1_RM64_IMM32)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        Operand destination = decode_rm64_operand(cursor, modrm, rex);
        Operand immediate = Operand::imm(static_cast<SignedQword>(cursor.read_i32()));

        if (modrm.reg == X86_GROUP1_ADD_EXTENSION)
        {
            instruction = Instruction::make_add(std::move(destination), std::move(immediate));
        }
        else if (modrm.reg == X86_GROUP1_SUB_EXTENSION)
        {
            instruction = Instruction::make_sub(std::move(destination), std::move(immediate));
        }
        else if (modrm.reg == X86_GROUP1_CMP_EXTENSION)
        {
            instruction = Instruction::make_cmp(std::move(destination), std::move(immediate));
        }
        else
        {
            throw DecodeError{DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE};
        }
    }
    else if (opcode == X86_OPCODE_JMP_REL8 || opcode == X86_OPCODE_JE_REL8 || opcode == X86_OPCODE_JNE_REL8)
    {
        const SignedQword displacement = static_cast<SignedQword>(cursor.read_i8());
        const Opcode decoded_opcode = opcode == X86_OPCODE_JMP_REL8
            ? Opcode::JMP
            : (opcode == X86_OPCODE_JE_REL8 ? Opcode::JE : Opcode::JNE);
        instruction = make_jump_instruction(decoded_opcode, relative_target(cursor.rip(), displacement));
    }
    else if (opcode == X86_OPCODE_JMP_REL32)
    {
        const SignedQword displacement = static_cast<SignedQword>(cursor.read_i32());
        instruction = make_jump_instruction(Opcode::JMP, relative_target(cursor.rip(), displacement));
    }
    else if (opcode == X86_OPCODE_ESCAPE)
    {
        const std::uint8_t escaped_opcode = cursor.read_u8();
        if (escaped_opcode != X86_OPCODE_JE_REL32 && escaped_opcode != X86_OPCODE_JNE_REL32)
        {
            throw DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
        }

        const SignedQword displacement = static_cast<SignedQword>(cursor.read_i32());
        const Opcode decoded_opcode = escaped_opcode == X86_OPCODE_JE_REL32 ? Opcode::JE : Opcode::JNE;
        instruction = make_jump_instruction(decoded_opcode, relative_target(cursor.rip(), displacement));
    }
    else
    {
        throw DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
    }

    return DecodedInstruction{std::move(instruction), rip, cursor.rip()};
}
}
