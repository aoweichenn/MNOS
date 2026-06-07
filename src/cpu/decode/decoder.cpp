#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <mnos/cpu/decode/decoder.hpp>

namespace
{
constexpr const char* DECODER_EMPTY_IMAGE_MESSAGE = "decoder cannot decode an empty executable image";
constexpr const char* DECODER_TRUNCATED_INSTRUCTION_MESSAGE = "decoder reached the end of image inside an instruction";
constexpr const char* DECODER_UNSUPPORTED_OPCODE_MESSAGE = "decoder unsupported x86-64 opcode";
constexpr const char* DECODER_REX_W_REQUIRED_MESSAGE = "decoder instruction requires REX.W";
constexpr const char* DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE = "decoder unsupported ModRM opcode extension";
constexpr const char* DECODER_UNSUPPORTED_LOCK_PREFIX_MESSAGE =
    "decoder LOCK prefix is only supported for x86-64 atomic read-modify-write instructions";
constexpr const char* DECODER_LOCK_PREFIX_REQUIRES_MEMORY_DESTINATION_MESSAGE =
    "decoder LOCK prefix requires a memory destination operand";
constexpr std::uint8_t X86_LOCK_PREFIX = 0xF0;
constexpr std::uint8_t X86_REX_PREFIX_MIN = 0x40;
constexpr std::uint8_t X86_REX_PREFIX_MAX = 0x4F;
constexpr std::uint8_t X86_REX_W_MASK = 0x08;
constexpr std::uint8_t X86_REX_R_MASK = 0x04;
constexpr std::uint8_t X86_REX_X_MASK = 0x02;
constexpr std::uint8_t X86_REX_B_MASK = 0x01;
constexpr std::uint8_t X86_REX_EXTENDED_REGISTER_OFFSET = 8;
constexpr std::uint8_t X86_OPCODE_ESCAPE = 0x0F;
constexpr std::uint8_t X86_OPCODE_HLT = 0xF4;
constexpr std::uint8_t X86_OPCODE_RET_NEAR = 0xC3;
constexpr std::uint8_t X86_OPCODE_INT3 = 0xCC;
constexpr std::uint8_t X86_OPCODE_INT_IMM8 = 0xCD;
constexpr std::uint8_t X86_OPCODE_IRET = 0xCF;
constexpr std::uint8_t X86_OPCODE_CALL_REL32 = 0xE8;
constexpr std::uint8_t X86_OPCODE_JMP_REL8 = 0xEB;
constexpr std::uint8_t X86_OPCODE_JMP_REL32 = 0xE9;
constexpr std::uint8_t X86_OPCODE_JCC_REL8_MIN = 0x70;
constexpr std::uint8_t X86_OPCODE_JCC_REL8_MAX = 0x7F;
constexpr std::uint8_t X86_OPCODE_JCC_REL32_MIN = 0x80;
constexpr std::uint8_t X86_OPCODE_JCC_REL32_MAX = 0x8F;
constexpr std::uint8_t X86_OPCODE_SYSCALL = 0x05;
constexpr std::uint8_t X86_OPCODE_SYSRET = 0x07;
constexpr std::uint8_t X86_OPCODE_CMOVCC_R64_RM64_MIN = 0x40;
constexpr std::uint8_t X86_OPCODE_CMOVCC_R64_RM64_MAX = 0x4F;
constexpr std::uint8_t X86_OPCODE_MFENCE_GROUP = 0xAE;
constexpr std::uint8_t X86_OPCODE_SETCC_RM8_MIN = 0x90;
constexpr std::uint8_t X86_OPCODE_SETCC_RM8_MAX = 0x9F;
constexpr std::uint8_t X86_OPCODE_CMPXCHG_RM64_R64 = 0xB1;
constexpr std::uint8_t X86_OPCODE_MOVZX_R64_RM8 = 0xB6;
constexpr std::uint8_t X86_OPCODE_MOVZX_R64_RM16 = 0xB7;
constexpr std::uint8_t X86_OPCODE_MOVSX_R64_RM8 = 0xBE;
constexpr std::uint8_t X86_OPCODE_MOVSX_R64_RM16 = 0xBF;
constexpr std::uint8_t X86_OPCODE_XADD_RM64_R64 = 0xC1;
constexpr std::uint8_t X86_OPCODE_CONDITION_MASK = 0x0F;
constexpr std::uint8_t X86_OPCODE_PUSH_R64_MIN = 0x50;
constexpr std::uint8_t X86_OPCODE_PUSH_R64_MAX = 0x57;
constexpr std::uint8_t X86_OPCODE_POP_R64_MIN = 0x58;
constexpr std::uint8_t X86_OPCODE_POP_R64_MAX = 0x5F;
constexpr std::uint8_t X86_OPCODE_PUSH_IMM32 = 0x68;
constexpr std::uint8_t X86_OPCODE_PUSH_IMM8 = 0x6A;
constexpr std::uint8_t X86_OPCODE_POP_RM64 = 0x8F;
constexpr std::uint8_t X86_OPCODE_MOVSXD_R64_RM32 = 0x63;
constexpr std::uint8_t X86_OPCODE_MOV_R64_IMM64_MIN = 0xB8;
constexpr std::uint8_t X86_OPCODE_MOV_R64_IMM64_MAX = 0xBF;
constexpr std::uint8_t X86_OPCODE_MOV_RM64_R64 = 0x89;
constexpr std::uint8_t X86_OPCODE_MOV_R64_RM64 = 0x8B;
constexpr std::uint8_t X86_OPCODE_LEA_R64_M = 0x8D;
constexpr std::uint8_t X86_OPCODE_ADD_RM64_R64 = 0x01;
constexpr std::uint8_t X86_OPCODE_ADD_R64_RM64 = 0x03;
constexpr std::uint8_t X86_OPCODE_OR_RM64_R64 = 0x09;
constexpr std::uint8_t X86_OPCODE_OR_R64_RM64 = 0x0B;
constexpr std::uint8_t X86_OPCODE_AND_RM64_R64 = 0x21;
constexpr std::uint8_t X86_OPCODE_AND_R64_RM64 = 0x23;
constexpr std::uint8_t X86_OPCODE_SUB_RM64_R64 = 0x29;
constexpr std::uint8_t X86_OPCODE_SUB_R64_RM64 = 0x2B;
constexpr std::uint8_t X86_OPCODE_XOR_RM64_R64 = 0x31;
constexpr std::uint8_t X86_OPCODE_XOR_R64_RM64 = 0x33;
constexpr std::uint8_t X86_OPCODE_CMP_RM64_R64 = 0x39;
constexpr std::uint8_t X86_OPCODE_CMP_R64_RM64 = 0x3B;
constexpr std::uint8_t X86_OPCODE_TEST_RM64_R64 = 0x85;
constexpr std::uint8_t X86_OPCODE_GROUP1_RM64_IMM32 = 0x81;
constexpr std::uint8_t X86_OPCODE_GROUP3_RM64 = 0xF7;
constexpr std::uint8_t X86_OPCODE_GROUP5_RM64 = 0xFF;
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
constexpr std::uint8_t X86_GROUP1_OR_EXTENSION = 1;
constexpr std::uint8_t X86_GROUP1_AND_EXTENSION = 4;
constexpr std::uint8_t X86_GROUP1_SUB_EXTENSION = 5;
constexpr std::uint8_t X86_GROUP1_XOR_EXTENSION = 6;
constexpr std::uint8_t X86_GROUP1_CMP_EXTENSION = 7;
constexpr std::uint8_t X86_GROUP3_TEST_EXTENSION = 0;
constexpr std::uint8_t X86_GROUP5_INC_EXTENSION = 0;
constexpr std::uint8_t X86_GROUP5_DEC_EXTENSION = 1;
constexpr std::uint8_t X86_GROUP5_CALL_EXTENSION = 2;
constexpr std::uint8_t X86_GROUP5_PUSH_EXTENSION = 6;
constexpr std::uint8_t X86_MFENCE_EXTENSION = 6;
constexpr std::uint8_t X86_MFENCE_RM_FIELD = 0;
constexpr std::uint8_t X86_POP_RM64_EXTENSION = 0;
constexpr std::size_t X86_REGISTER_COUNT = 16;

struct RexPrefix
{
    bool w = false;
    bool r = false;
    bool x = false;
    bool b = false;
};

struct InstructionPrefixes
{
    RexPrefix rex;
    bool locked = false;
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

enum class BinaryInstructionKind : std::uint8_t
{
    MOV,
    ADD,
    SUB,
    CMP,
    AND,
    OR,
    XOR,
    TEST
};

enum class UnaryInstructionKind : std::uint8_t
{
    INC,
    DEC,
    PUSH,
    POP,
    CALL
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

[[nodiscard]] InstructionPrefixes read_instruction_prefixes(DecodeCursor& cursor)
{
    InstructionPrefixes prefixes;
    bool keep_reading = true;
    while (keep_reading)
    {
        const std::uint8_t value = cursor.peek_u8();
        if (value == X86_LOCK_PREFIX)
        {
            static_cast<void>(cursor.read_u8());
            prefixes.locked = true;
        }
        else if (is_rex_prefix(value))
        {
            static_cast<void>(cursor.read_u8());
            prefixes.rex.w = (value & X86_REX_W_MASK) != 0;
            prefixes.rex.r = (value & X86_REX_R_MASK) != 0;
            prefixes.rex.x = (value & X86_REX_X_MASK) != 0;
            prefixes.rex.b = (value & X86_REX_B_MASK) != 0;
        }
        else
        {
            keep_reading = false;
        }
    }
    return prefixes;
}

void require_rex_w(const RexPrefix& rex)
{
    if (!rex.w)
    {
        throw mnos::cpu::DecodeError{DECODER_REX_W_REQUIRED_MESSAGE};
    }
}

void require_unlocked(const bool locked)
{
    if (locked)
    {
        throw mnos::cpu::DecodeError{DECODER_UNSUPPORTED_LOCK_PREFIX_MESSAGE};
    }
}

void require_lockable_memory_destination(const mnos::cpu::Operand& destination, const bool locked)
{
    if (locked && !destination.is_memory())
    {
        throw mnos::cpu::DecodeError{DECODER_LOCK_PREFIX_REQUIRES_MEMORY_DESTINATION_MESSAGE};
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
    const RexPrefix& rex,
    const mnos::cpu::DataSize data_size)
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
            data_size);
    }

    if (has_base)
    {
        return mnos::cpu::Operand::mem(decode_register(sib.base, rex.b), displacement, data_size);
    }

    if (has_index)
    {
        return mnos::cpu::Operand::mem(
            mnos::cpu::MemoryAddress::index_displacement(
                decode_register(sib.index, rex.x),
                sib_scale_to_multiplier(sib.scale),
                displacement),
            data_size);
    }

    return mnos::cpu::Operand::absolute_mem(static_cast<mnos::cpu::Address64>(displacement), data_size);
}

[[nodiscard]] mnos::cpu::Operand decode_rm_operand(
    DecodeCursor& cursor,
    const ModRmByte& modrm,
    const RexPrefix& rex,
    const mnos::cpu::DataSize data_size)
{
    if (modrm.mod == X86_MODRM_MOD_REGISTER)
    {
        return mnos::cpu::Operand::reg(decode_register(modrm.rm, rex.b), data_size);
    }

    if (modrm.rm == X86_MODRM_RM_SIB)
    {
        return make_memory_operand_from_sib(cursor, modrm, rex, data_size);
    }

    if (modrm.mod == X86_MODRM_MOD_NO_DISPLACEMENT && modrm.rm == X86_MODRM_RM_RIP_RELATIVE)
    {
        const mnos::cpu::SignedQword displacement = static_cast<mnos::cpu::SignedQword>(cursor.read_i32());
        const mnos::cpu::Address64 address =
            cursor.rip() + static_cast<mnos::cpu::Address64>(displacement);
        return mnos::cpu::Operand::absolute_mem(address, data_size);
    }

    return mnos::cpu::Operand::mem(
        decode_register(modrm.rm, rex.b),
        read_modrm_displacement(cursor, modrm),
        data_size);
}

[[nodiscard]] mnos::cpu::Operand decode_reg_operand(
    const std::uint8_t encoded,
    const RexPrefix& rex,
    const mnos::cpu::DataSize data_size)
{
    return mnos::cpu::Operand::reg(decode_register(encoded, rex.r), data_size);
}

[[nodiscard]] mnos::cpu::InstructionPointer relative_target(
    const mnos::cpu::InstructionPointer next_rip,
    const mnos::cpu::SignedQword displacement) noexcept
{
    return next_rip + static_cast<mnos::cpu::InstructionPointer>(displacement);
}

[[nodiscard]] mnos::cpu::Instruction make_binary_instruction(
    const BinaryInstructionKind kind,
    mnos::cpu::Operand destination,
    mnos::cpu::Operand source)
{
    switch (kind)
    {
    case BinaryInstructionKind::MOV:
        return mnos::cpu::Instruction::make_mov(std::move(destination), std::move(source));
    case BinaryInstructionKind::ADD:
        return mnos::cpu::Instruction::make_add(std::move(destination), std::move(source));
    case BinaryInstructionKind::SUB:
        return mnos::cpu::Instruction::make_sub(std::move(destination), std::move(source));
    case BinaryInstructionKind::CMP:
        return mnos::cpu::Instruction::make_cmp(std::move(destination), std::move(source));
    case BinaryInstructionKind::AND:
        return mnos::cpu::Instruction::make_and(std::move(destination), std::move(source));
    case BinaryInstructionKind::OR:
        return mnos::cpu::Instruction::make_or(std::move(destination), std::move(source));
    case BinaryInstructionKind::XOR:
        return mnos::cpu::Instruction::make_xor(std::move(destination), std::move(source));
    case BinaryInstructionKind::TEST:
        return mnos::cpu::Instruction::make_test(std::move(destination), std::move(source));
    }
}

[[nodiscard]] mnos::cpu::Instruction make_unary_instruction(
    const UnaryInstructionKind kind,
    mnos::cpu::Operand operand)
{
    switch (kind)
    {
    case UnaryInstructionKind::INC:
        return mnos::cpu::Instruction::make_inc(std::move(operand));
    case UnaryInstructionKind::DEC:
        return mnos::cpu::Instruction::make_dec(std::move(operand));
    case UnaryInstructionKind::PUSH:
        return mnos::cpu::Instruction::make_push(std::move(operand));
    case UnaryInstructionKind::POP:
        return mnos::cpu::Instruction::make_pop(std::move(operand));
    case UnaryInstructionKind::CALL:
        return mnos::cpu::Instruction::make_call(std::move(operand));
    }
}

[[nodiscard]] mnos::cpu::ConditionCode condition_from_opcode(const std::uint8_t opcode) noexcept
{
    return static_cast<mnos::cpu::ConditionCode>(opcode & X86_OPCODE_CONDITION_MASK);
}

[[nodiscard]] mnos::cpu::Instruction make_conditional_jump_instruction(
    const mnos::cpu::ConditionCode condition,
    const mnos::cpu::InstructionPointer target)
{
    const auto immediate = static_cast<mnos::cpu::SignedQword>(target);
    if (condition == mnos::cpu::ConditionCode::E)
    {
        return mnos::cpu::Instruction::make_je(mnos::cpu::Operand::imm(immediate));
    }

    if (condition == mnos::cpu::ConditionCode::NE)
    {
        return mnos::cpu::Instruction::make_jne(mnos::cpu::Operand::imm(immediate));
    }

    return mnos::cpu::Instruction::make_jcc(condition, mnos::cpu::Operand::imm(immediate));
}

[[nodiscard]] mnos::cpu::Instruction make_unconditional_jump_instruction(
    const mnos::cpu::InstructionPointer target)
{
    return mnos::cpu::Instruction::make_jmp(
        mnos::cpu::Operand::imm(static_cast<mnos::cpu::SignedQword>(target)));
}

[[nodiscard]] bool rm_operand_is_destination_opcode(const std::uint8_t opcode) noexcept
{
    return opcode == X86_OPCODE_MOV_RM64_R64 || opcode == X86_OPCODE_ADD_RM64_R64 ||
        opcode == X86_OPCODE_OR_RM64_R64 || opcode == X86_OPCODE_AND_RM64_R64 ||
        opcode == X86_OPCODE_SUB_RM64_R64 || opcode == X86_OPCODE_XOR_RM64_R64 ||
        opcode == X86_OPCODE_CMP_RM64_R64 || opcode == X86_OPCODE_TEST_RM64_R64;
}

[[nodiscard]] std::optional<BinaryInstructionKind> binary_kind_from_opcode(const std::uint8_t opcode) noexcept
{
    switch (opcode)
    {
    case X86_OPCODE_MOV_RM64_R64:
    case X86_OPCODE_MOV_R64_RM64:
        return BinaryInstructionKind::MOV;
    case X86_OPCODE_ADD_RM64_R64:
    case X86_OPCODE_ADD_R64_RM64:
        return BinaryInstructionKind::ADD;
    case X86_OPCODE_OR_RM64_R64:
    case X86_OPCODE_OR_R64_RM64:
        return BinaryInstructionKind::OR;
    case X86_OPCODE_AND_RM64_R64:
    case X86_OPCODE_AND_R64_RM64:
        return BinaryInstructionKind::AND;
    case X86_OPCODE_SUB_RM64_R64:
    case X86_OPCODE_SUB_R64_RM64:
        return BinaryInstructionKind::SUB;
    case X86_OPCODE_XOR_RM64_R64:
    case X86_OPCODE_XOR_R64_RM64:
        return BinaryInstructionKind::XOR;
    case X86_OPCODE_CMP_RM64_R64:
    case X86_OPCODE_CMP_R64_RM64:
        return BinaryInstructionKind::CMP;
    case X86_OPCODE_TEST_RM64_R64:
        return BinaryInstructionKind::TEST;
    default:
        break;
    }

    return std::nullopt;
}

[[nodiscard]] mnos::cpu::Instruction make_group1_instruction(
    const std::uint8_t extension,
    mnos::cpu::Operand destination,
    mnos::cpu::Operand immediate)
{
    if (extension == X86_GROUP1_ADD_EXTENSION)
    {
        return mnos::cpu::Instruction::make_add(std::move(destination), std::move(immediate));
    }

    if (extension == X86_GROUP1_OR_EXTENSION)
    {
        return mnos::cpu::Instruction::make_or(std::move(destination), std::move(immediate));
    }

    if (extension == X86_GROUP1_AND_EXTENSION)
    {
        return mnos::cpu::Instruction::make_and(std::move(destination), std::move(immediate));
    }

    if (extension == X86_GROUP1_SUB_EXTENSION)
    {
        return mnos::cpu::Instruction::make_sub(std::move(destination), std::move(immediate));
    }

    if (extension == X86_GROUP1_XOR_EXTENSION)
    {
        return mnos::cpu::Instruction::make_xor(std::move(destination), std::move(immediate));
    }

    if (extension == X86_GROUP1_CMP_EXTENSION)
    {
        return mnos::cpu::Instruction::make_cmp(std::move(destination), std::move(immediate));
    }

    throw mnos::cpu::DecodeError{DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE};
}

[[nodiscard]] mnos::cpu::Instruction make_group5_instruction(
    const std::uint8_t extension,
    mnos::cpu::Operand operand)
{
    if (extension == X86_GROUP5_INC_EXTENSION)
    {
        return make_unary_instruction(UnaryInstructionKind::INC, std::move(operand));
    }

    if (extension == X86_GROUP5_DEC_EXTENSION)
    {
        return make_unary_instruction(UnaryInstructionKind::DEC, std::move(operand));
    }

    if (extension == X86_GROUP5_CALL_EXTENSION)
    {
        return make_unary_instruction(UnaryInstructionKind::CALL, std::move(operand));
    }

    if (extension == X86_GROUP5_PUSH_EXTENSION)
    {
        return make_unary_instruction(UnaryInstructionKind::PUSH, std::move(operand));
    }

    throw mnos::cpu::DecodeError{DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE};
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
    const InstructionPrefixes prefixes = read_instruction_prefixes(cursor);
    const RexPrefix& rex = prefixes.rex;
    const std::uint8_t opcode = cursor.read_u8();
    Instruction instruction = Instruction::make_hlt();

    if (prefixes.locked && opcode != X86_OPCODE_ESCAPE)
    {
        throw DecodeError{DECODER_UNSUPPORTED_LOCK_PREFIX_MESSAGE};
    }

    if (opcode == X86_OPCODE_HLT)
    {
        instruction = Instruction::make_hlt();
    }
    else if (opcode == X86_OPCODE_INT3)
    {
        instruction = Instruction::make_int(system::InterruptVector::breakpoint());
    }
    else if (opcode == X86_OPCODE_INT_IMM8)
    {
        instruction = Instruction::make_int(system::InterruptVector{cursor.read_u8()});
    }
    else if (opcode == X86_OPCODE_RET_NEAR)
    {
        instruction = Instruction::make_ret();
    }
    else if (opcode == X86_OPCODE_IRET)
    {
        instruction = Instruction::make_iret();
    }
    else if (opcode >= X86_OPCODE_PUSH_R64_MIN && opcode <= X86_OPCODE_PUSH_R64_MAX)
    {
        const RegisterId source = decode_register(opcode - X86_OPCODE_PUSH_R64_MIN, rex.b);
        instruction = Instruction::make_push(Operand::reg(source));
    }
    else if (opcode >= X86_OPCODE_POP_R64_MIN && opcode <= X86_OPCODE_POP_R64_MAX)
    {
        const RegisterId destination = decode_register(opcode - X86_OPCODE_POP_R64_MIN, rex.b);
        instruction = Instruction::make_pop(Operand::reg(destination));
    }
    else if (opcode == X86_OPCODE_PUSH_IMM8)
    {
        instruction = Instruction::make_push(Operand::imm(static_cast<SignedQword>(cursor.read_i8())));
    }
    else if (opcode == X86_OPCODE_PUSH_IMM32)
    {
        instruction = Instruction::make_push(Operand::imm(static_cast<SignedQword>(cursor.read_i32())));
    }
    else if (opcode == X86_OPCODE_CALL_REL32)
    {
        const SignedQword displacement = static_cast<SignedQword>(cursor.read_i32());
        instruction = Instruction::make_call(
            Operand::imm(static_cast<SignedQword>(relative_target(cursor.rip(), displacement))));
    }
    else if (opcode >= X86_OPCODE_MOV_R64_IMM64_MIN && opcode <= X86_OPCODE_MOV_R64_IMM64_MAX)
    {
        require_rex_w(rex);
        const RegisterId destination = decode_register(opcode - X86_OPCODE_MOV_R64_IMM64_MIN, rex.b);
        instruction = Instruction::make_mov(
            Operand::reg(destination),
            Operand::imm(qword_to_signed_immediate(cursor.read_u64())));
    }
    else if (opcode == X86_OPCODE_MOVSXD_R64_RM32)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        instruction = Instruction::make_movsx(
            decode_reg_operand(modrm.reg, rex, DataSize::QWORD),
            decode_rm_operand(cursor, modrm, rex, DataSize::DWORD));
    }
    else if (opcode == X86_OPCODE_LEA_R64_M)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        Operand source = decode_rm_operand(cursor, modrm, rex, DataSize::QWORD);
        if (!source.is_memory())
        {
            throw DecodeError{DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE};
        }
        instruction = Instruction::make_lea(decode_reg_operand(modrm.reg, rex, DataSize::QWORD), std::move(source));
    }
    else if (const std::optional<BinaryInstructionKind> decoded_kind = binary_kind_from_opcode(opcode);
             decoded_kind.has_value())
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        Operand rm_operand = decode_rm_operand(cursor, modrm, rex, DataSize::QWORD);
        Operand reg_operand = decode_reg_operand(modrm.reg, rex, DataSize::QWORD);

        if (rm_operand_is_destination_opcode(opcode))
        {
            instruction = make_binary_instruction(decoded_kind.value(), std::move(rm_operand), std::move(reg_operand));
        }
        else
        {
            instruction = make_binary_instruction(decoded_kind.value(), std::move(reg_operand), std::move(rm_operand));
        }
    }
    else if (opcode == X86_OPCODE_GROUP1_RM64_IMM32)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        Operand destination = decode_rm_operand(cursor, modrm, rex, DataSize::QWORD);
        Operand immediate = Operand::imm(static_cast<SignedQword>(cursor.read_i32()));
        instruction = make_group1_instruction(modrm.reg, std::move(destination), std::move(immediate));
    }
    else if (opcode == X86_OPCODE_GROUP3_RM64)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        if (modrm.reg != X86_GROUP3_TEST_EXTENSION)
        {
            throw DecodeError{DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE};
        }

        Operand left = decode_rm_operand(cursor, modrm, rex, DataSize::QWORD);
        Operand immediate = Operand::imm(static_cast<SignedQword>(cursor.read_i32()));
        instruction = Instruction::make_test(std::move(left), std::move(immediate));
    }
    else if (opcode == X86_OPCODE_GROUP5_RM64)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        Operand operand = decode_rm_operand(cursor, modrm, rex, DataSize::QWORD);
        instruction = make_group5_instruction(modrm.reg, std::move(operand));
    }
    else if (opcode == X86_OPCODE_POP_RM64)
    {
        require_rex_w(rex);
        const ModRmByte modrm = read_modrm(cursor);
        if (modrm.reg != X86_POP_RM64_EXTENSION)
        {
            throw DecodeError{DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE};
        }

        instruction = Instruction::make_pop(decode_rm_operand(cursor, modrm, rex, DataSize::QWORD));
    }
    else if (opcode >= X86_OPCODE_JCC_REL8_MIN && opcode <= X86_OPCODE_JCC_REL8_MAX)
    {
        const SignedQword displacement = static_cast<SignedQword>(cursor.read_i8());
        instruction = make_conditional_jump_instruction(
            condition_from_opcode(opcode),
            relative_target(cursor.rip(), displacement));
    }
    else if (opcode == X86_OPCODE_JMP_REL32)
    {
        const SignedQword displacement = static_cast<SignedQword>(cursor.read_i32());
        instruction = make_unconditional_jump_instruction(relative_target(cursor.rip(), displacement));
    }
    else if (opcode == X86_OPCODE_JMP_REL8)
    {
        const SignedQword displacement = static_cast<SignedQword>(cursor.read_i8());
        instruction = make_unconditional_jump_instruction(relative_target(cursor.rip(), displacement));
    }
    else if (opcode == X86_OPCODE_ESCAPE)
    {
        const std::uint8_t escaped_opcode = cursor.read_u8();
        if (escaped_opcode == X86_OPCODE_SYSCALL)
        {
            require_unlocked(prefixes.locked);
            instruction = Instruction::make_syscall();
        }
        else if (escaped_opcode == X86_OPCODE_SYSRET)
        {
            require_unlocked(prefixes.locked);
            require_rex_w(rex);
            instruction = Instruction::make_sysret();
        }
        else if (escaped_opcode >= X86_OPCODE_JCC_REL32_MIN && escaped_opcode <= X86_OPCODE_JCC_REL32_MAX)
        {
            require_unlocked(prefixes.locked);
            const SignedQword displacement = static_cast<SignedQword>(cursor.read_i32());
            instruction = make_conditional_jump_instruction(
                condition_from_opcode(escaped_opcode),
                relative_target(cursor.rip(), displacement));
        }
        else if (escaped_opcode >= X86_OPCODE_CMOVCC_R64_RM64_MIN && escaped_opcode <= X86_OPCODE_CMOVCC_R64_RM64_MAX)
        {
            require_unlocked(prefixes.locked);
            require_rex_w(rex);
            const ModRmByte modrm = read_modrm(cursor);
            instruction = Instruction::make_cmovcc(
                condition_from_opcode(escaped_opcode),
                decode_reg_operand(modrm.reg, rex, DataSize::QWORD),
                decode_rm_operand(cursor, modrm, rex, DataSize::QWORD));
        }
        else if (escaped_opcode >= X86_OPCODE_SETCC_RM8_MIN && escaped_opcode <= X86_OPCODE_SETCC_RM8_MAX)
        {
            require_unlocked(prefixes.locked);
            const ModRmByte modrm = read_modrm(cursor);
            instruction = Instruction::make_setcc(
                condition_from_opcode(escaped_opcode),
                decode_rm_operand(cursor, modrm, rex, DataSize::BYTE));
        }
        else if (escaped_opcode == X86_OPCODE_CMPXCHG_RM64_R64 || escaped_opcode == X86_OPCODE_XADD_RM64_R64)
        {
            require_rex_w(rex);
            const ModRmByte modrm = read_modrm(cursor);
            Operand destination = decode_rm_operand(cursor, modrm, rex, DataSize::QWORD);
            require_lockable_memory_destination(destination, prefixes.locked);
            Operand source = decode_reg_operand(modrm.reg, rex, DataSize::QWORD);
            if (escaped_opcode == X86_OPCODE_CMPXCHG_RM64_R64)
            {
                instruction = Instruction::make_cmpxchg(std::move(destination), std::move(source), prefixes.locked);
            }
            else
            {
                instruction = Instruction::make_xadd(std::move(destination), std::move(source), prefixes.locked);
            }
        }
        else if (escaped_opcode == X86_OPCODE_MFENCE_GROUP)
        {
            require_unlocked(prefixes.locked);
            const ModRmByte modrm = read_modrm(cursor);
            if (modrm.mod != X86_MODRM_MOD_REGISTER || modrm.reg != X86_MFENCE_EXTENSION ||
                modrm.rm != X86_MFENCE_RM_FIELD)
            {
                throw DecodeError{DECODER_UNSUPPORTED_MODRM_EXTENSION_MESSAGE};
            }
            instruction = Instruction::make_mfence();
        }
        else if (escaped_opcode == X86_OPCODE_MOVZX_R64_RM8 || escaped_opcode == X86_OPCODE_MOVZX_R64_RM16 ||
                 escaped_opcode == X86_OPCODE_MOVSX_R64_RM8 || escaped_opcode == X86_OPCODE_MOVSX_R64_RM16)
        {
            require_unlocked(prefixes.locked);
            require_rex_w(rex);
            const DataSize source_size =
                escaped_opcode == X86_OPCODE_MOVZX_R64_RM8 || escaped_opcode == X86_OPCODE_MOVSX_R64_RM8
                ? DataSize::BYTE
                : DataSize::WORD;
            const ModRmByte modrm = read_modrm(cursor);
            Operand destination = decode_reg_operand(modrm.reg, rex, DataSize::QWORD);
            Operand source = decode_rm_operand(cursor, modrm, rex, source_size);

            if (escaped_opcode == X86_OPCODE_MOVZX_R64_RM8 || escaped_opcode == X86_OPCODE_MOVZX_R64_RM16)
            {
                instruction = Instruction::make_movzx(std::move(destination), std::move(source));
            }
            else
            {
                instruction = Instruction::make_movsx(std::move(destination), std::move(source));
            }
        }
        else
        {
            throw DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
        }
    }
    else
    {
        throw DecodeError{DECODER_UNSUPPORTED_OPCODE_MESSAGE};
    }

    return DecodedInstruction{std::move(instruction), rip, cursor.rip()};
}
}
