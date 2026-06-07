#include <cstdint>
#include <limits>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/decode/decoder.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/instruction/condition_code.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>

namespace cpu = mnos::cpu;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr cpu::SignedQword TEST_IMMEDIATE_VALUE = cpu::SignedQword{42};
constexpr cpu::InstructionPointer TEST_RIP_ZERO = cpu::InstructionPointer{0};
constexpr cpu::InstructionPointer TEST_IMAGE_BASE_RIP = cpu::InstructionPointer{0x1000};
constexpr cpu::InstructionPointer TEST_IMAGE_SECOND_RIP = TEST_IMAGE_BASE_RIP + cpu::InstructionPointer{1};
constexpr cpu::InstructionPointer TEST_IMAGE_END_RIP = TEST_IMAGE_BASE_RIP + cpu::InstructionPointer{2};
constexpr cpu::InstructionPointer TEST_MAX_RIP = std::numeric_limits<cpu::InstructionPointer>::max();
constexpr cpu::InstructionPointer TEST_HLT_NEXT_RIP = cpu::InstructionPointer{1};
constexpr cpu::InstructionPointer TEST_MOV_IMM_NEXT_RIP = cpu::InstructionPointer{10};
constexpr cpu::InstructionPointer TEST_RIP_RELATIVE_NEXT_RIP = cpu::InstructionPointer{7};
constexpr cpu::Address64 TEST_RIP_RELATIVE_ADDRESS = cpu::Address64{11};
constexpr cpu::SignedQword TEST_REL8_FORWARD_TARGET = cpu::SignedQword{5};
constexpr cpu::SignedQword TEST_REL8_BACKWARD_TARGET = cpu::SignedQword{0};
constexpr cpu::SignedQword TEST_REL32_FORWARD_TARGET = cpu::SignedQword{11};
constexpr cpu::SignedQword TEST_JMP_REL32_FORWARD_TARGET = cpu::SignedQword{10};
constexpr cpu::SignedQword TEST_DISPLACEMENT_VALUE = cpu::SignedQword{8};
constexpr cpu::SignedQword TEST_DISP32_VALUE = cpu::SignedQword{0x12345678};
constexpr std::uint8_t TEST_SIB_SCALE_VALUE = cpu::MEMORY_ADDRESS_SCALE_4;
}

TEST(ExecutableImageTest, ModelsByteImageAddressRange)
{
    const cpu::ExecutableImage image{
        cpu::ExecutableImage::container_type{0xF4, 0x90},
        TEST_IMAGE_BASE_RIP};
    const cpu::ExecutableImage zero_based_image{cpu::ExecutableImage::container_type{0xF4}};

    EXPECT_FALSE(image.empty());
    EXPECT_THAT(zero_based_image.base_rip(), Eq(TEST_RIP_ZERO));
    EXPECT_THAT(zero_based_image.byte_at(TEST_RIP_ZERO), Eq(cpu::Byte{0xF4}));
    EXPECT_THAT(image.size(), Eq(std::size_t{2}));
    EXPECT_THAT(image.base_rip(), Eq(TEST_IMAGE_BASE_RIP));
    EXPECT_THAT(image.end_rip(), Eq(TEST_IMAGE_END_RIP));
    EXPECT_TRUE(image.contains_rip(TEST_IMAGE_BASE_RIP));
    EXPECT_TRUE(image.contains_rip(TEST_IMAGE_SECOND_RIP));
    EXPECT_FALSE(image.contains_rip(TEST_RIP_ZERO));
    EXPECT_FALSE(image.contains_rip(TEST_IMAGE_END_RIP));
    EXPECT_THAT(image.offset_of(TEST_IMAGE_SECOND_RIP), Eq(std::size_t{1}));
    EXPECT_THAT(image.byte_at(TEST_IMAGE_BASE_RIP), Eq(cpu::Byte{0xF4}));
    EXPECT_THAT(image.bytes().size(), Eq(std::size_t{2}));
    EXPECT_THROW(static_cast<void>(image.byte_at(TEST_RIP_ZERO)), std::out_of_range);

    const cpu::ExecutableImage boundary_image{
        cpu::ExecutableImage::container_type{0xF4},
        TEST_MAX_RIP};
    EXPECT_TRUE(boundary_image.contains_rip(TEST_MAX_RIP));
    EXPECT_FALSE(boundary_image.contains_rip(TEST_RIP_ZERO));
}

TEST(DecoderTest, DecodesHltAndMovImmediate)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction hlt = decoder.decode(cpu::ExecutableImage{0xF4}, TEST_RIP_ZERO);
    EXPECT_THAT(hlt.instruction.opcode(), Eq(cpu::Opcode::HLT));
    EXPECT_THAT(hlt.next_rip, Eq(TEST_HLT_NEXT_RIP));

    const cpu::DecodedInstruction mov_rax = decoder.decode(
        cpu::ExecutableImage{0x48, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        TEST_RIP_ZERO);
    EXPECT_THAT(mov_rax.instruction.opcode(), Eq(cpu::Opcode::MOV));
    EXPECT_THAT(mov_rax.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(mov_rax.instruction.second_operand().immediate_value(), Eq(TEST_IMMEDIATE_VALUE));
    EXPECT_THAT(mov_rax.next_rip, Eq(TEST_MOV_IMM_NEXT_RIP));

    const cpu::DecodedInstruction mov_r8 = decoder.decode(
        cpu::ExecutableImage{0x49, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        TEST_RIP_ZERO);
    EXPECT_THAT(mov_r8.instruction.first_operand().register_id(), Eq(cpu::RegisterId::R8));
}

TEST(DecoderTest, DecodesModRmRegisterMemoryAndImmediateForms)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction mov_store =
        decoder.decode(cpu::ExecutableImage{0x48, 0x89, 0x45, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(mov_store.instruction.opcode(), Eq(cpu::Opcode::MOV));
    EXPECT_TRUE(mov_store.instruction.first_operand().is_memory());
    EXPECT_THAT(mov_store.instruction.first_operand().memory_base_register(), Eq(cpu::RegisterId::RBP));
    EXPECT_THAT(mov_store.instruction.first_operand().memory_displacement(), Eq(TEST_DISPLACEMENT_VALUE));
    EXPECT_THAT(mov_store.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction mov_load =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x5D, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(mov_load.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));
    EXPECT_THAT(mov_load.instruction.second_operand().memory_base_register(), Eq(cpu::RegisterId::RBP));

    const cpu::DecodedInstruction add_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x01, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(add_reg.instruction.opcode(), Eq(cpu::Opcode::ADD));
    EXPECT_THAT(add_reg.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(add_reg.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RBX));

    const cpu::DecodedInstruction cmp_imm =
        decoder.decode(cpu::ExecutableImage{0x48, 0x81, 0xF8, 0x2A, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(cmp_imm.instruction.opcode(), Eq(cpu::Opcode::CMP));
    EXPECT_THAT(cmp_imm.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(cmp_imm.instruction.second_operand().immediate_value(), Eq(TEST_IMMEDIATE_VALUE));
}

TEST(DecoderTest, DecodesSibAndRipRelativeMemory)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction rsp_sib =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x44, 0x24, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(rsp_sib.instruction.second_operand().memory_base_register(), Eq(cpu::RegisterId::RSP));
    EXPECT_FALSE(rsp_sib.instruction.second_operand().memory_has_index_register());
    EXPECT_THAT(rsp_sib.instruction.second_operand().memory_displacement(), Eq(TEST_DISPLACEMENT_VALUE));

    const cpu::DecodedInstruction indexed_sib =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x44, 0x85, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(indexed_sib.instruction.second_operand().memory_base_register(), Eq(cpu::RegisterId::RBP));
    EXPECT_THAT(indexed_sib.instruction.second_operand().memory_index_register(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(indexed_sib.instruction.second_operand().memory_scale(), Eq(TEST_SIB_SCALE_VALUE));

    const cpu::DecodedInstruction rip_relative =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x05, 0x04, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_TRUE(rip_relative.instruction.second_operand().memory_has_absolute_address());
    EXPECT_THAT(rip_relative.instruction.second_operand().memory_absolute_address(), Eq(TEST_RIP_RELATIVE_ADDRESS));
    EXPECT_THAT(rip_relative.next_rip, Eq(TEST_RIP_RELATIVE_NEXT_RIP));
}

TEST(DecoderTest, DecodesDisp32NoDispAndNoBaseSibMemory)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction disp32 =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x9D, 0x78, 0x56, 0x34, 0x12}, TEST_RIP_ZERO);
    EXPECT_THAT(disp32.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));
    EXPECT_THAT(disp32.instruction.second_operand().memory_base_register(), Eq(cpu::RegisterId::RBP));
    EXPECT_THAT(disp32.instruction.second_operand().memory_displacement(), Eq(TEST_DISP32_VALUE));

    const cpu::DecodedInstruction no_disp =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x03}, TEST_RIP_ZERO);
    EXPECT_THAT(no_disp.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(no_disp.instruction.second_operand().memory_base_register(), Eq(cpu::RegisterId::RBX));
    EXPECT_THAT(no_disp.instruction.second_operand().memory_displacement(), Eq(cpu::SignedQword{0}));

    const cpu::DecodedInstruction index_only_sib =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x1C, 0x85, 0x08, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_FALSE(index_only_sib.instruction.second_operand().memory_has_base_register());
    EXPECT_TRUE(index_only_sib.instruction.second_operand().memory_has_index_register());
    EXPECT_THAT(index_only_sib.instruction.second_operand().memory_index_register(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(index_only_sib.instruction.second_operand().memory_scale(), Eq(TEST_SIB_SCALE_VALUE));
    EXPECT_THAT(index_only_sib.instruction.second_operand().memory_displacement(), Eq(TEST_DISPLACEMENT_VALUE));

    const cpu::DecodedInstruction absolute_sib =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8B, 0x1C, 0x25, 0x08, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_TRUE(absolute_sib.instruction.second_operand().memory_has_absolute_address());
    EXPECT_THAT(absolute_sib.instruction.second_operand().memory_absolute_address(), Eq(cpu::Address64{8}));
}

TEST(DecoderTest, DecodesAdditionalArithmeticForms)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction add_reg_mem =
        decoder.decode(cpu::ExecutableImage{0x48, 0x03, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(add_reg_mem.instruction.opcode(), Eq(cpu::Opcode::ADD));
    EXPECT_THAT(add_reg_mem.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));
    EXPECT_THAT(add_reg_mem.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction sub_mem_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x29, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(sub_mem_reg.instruction.opcode(), Eq(cpu::Opcode::SUB));
    EXPECT_THAT(sub_mem_reg.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(sub_mem_reg.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RBX));

    const cpu::DecodedInstruction cmp_mem_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x39, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(cmp_mem_reg.instruction.opcode(), Eq(cpu::Opcode::CMP));
    EXPECT_THAT(cmp_mem_reg.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(cmp_mem_reg.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RBX));

    const cpu::DecodedInstruction sub_reg_mem =
        decoder.decode(cpu::ExecutableImage{0x48, 0x2B, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(sub_reg_mem.instruction.opcode(), Eq(cpu::Opcode::SUB));
    EXPECT_THAT(sub_reg_mem.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));
    EXPECT_THAT(sub_reg_mem.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction cmp_reg_mem =
        decoder.decode(cpu::ExecutableImage{0x48, 0x3B, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(cmp_reg_mem.instruction.opcode(), Eq(cpu::Opcode::CMP));
    EXPECT_THAT(cmp_reg_mem.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));
    EXPECT_THAT(cmp_reg_mem.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction sub_imm =
        decoder.decode(cpu::ExecutableImage{0x48, 0x81, 0xE8, 0x01, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(sub_imm.instruction.opcode(), Eq(cpu::Opcode::SUB));
    EXPECT_THAT(sub_imm.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(sub_imm.instruction.second_operand().immediate_value(), Eq(cpu::SignedQword{1}));
}

TEST(DecoderTest, DecodesRelativeBranches)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction jmp_rel8 = decoder.decode(cpu::ExecutableImage{0xEB, 0x03, 0xF4}, TEST_RIP_ZERO);
    EXPECT_THAT(jmp_rel8.instruction.opcode(), Eq(cpu::Opcode::JMP));
    EXPECT_THAT(jmp_rel8.instruction.first_operand().immediate_value(), Eq(TEST_REL8_FORWARD_TARGET));

    const cpu::DecodedInstruction je_rel8 = decoder.decode(cpu::ExecutableImage{0x74, 0xFE}, TEST_RIP_ZERO);
    EXPECT_THAT(je_rel8.instruction.opcode(), Eq(cpu::Opcode::JE));
    EXPECT_THAT(je_rel8.instruction.first_operand().immediate_value(), Eq(TEST_REL8_BACKWARD_TARGET));

    const cpu::DecodedInstruction jne_rel32 =
        decoder.decode(cpu::ExecutableImage{0x0F, 0x85, 0x05, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(jne_rel32.instruction.opcode(), Eq(cpu::Opcode::JNE));
    EXPECT_THAT(jne_rel32.instruction.first_operand().immediate_value(), Eq(TEST_REL32_FORWARD_TARGET));

    const cpu::DecodedInstruction jmp_rel32 =
        decoder.decode(cpu::ExecutableImage{0xE9, 0x05, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(jmp_rel32.instruction.opcode(), Eq(cpu::Opcode::JMP));
    EXPECT_THAT(jmp_rel32.instruction.first_operand().immediate_value(), Eq(TEST_JMP_REL32_FORWARD_TARGET));
}

TEST(DecoderTest, DecodesStackControlAndLeaForms)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction push_rax = decoder.decode(cpu::ExecutableImage{0x50}, TEST_RIP_ZERO);
    EXPECT_THAT(push_rax.instruction.opcode(), Eq(cpu::Opcode::PUSH));
    EXPECT_THAT(push_rax.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction push_r8 = decoder.decode(cpu::ExecutableImage{0x41, 0x50}, TEST_RIP_ZERO);
    EXPECT_THAT(push_r8.instruction.first_operand().register_id(), Eq(cpu::RegisterId::R8));

    const cpu::DecodedInstruction push_imm32 =
        decoder.decode(cpu::ExecutableImage{0x68, 0x2A, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(push_imm32.instruction.opcode(), Eq(cpu::Opcode::PUSH));
    EXPECT_THAT(push_imm32.instruction.first_operand().immediate_value(), Eq(TEST_IMMEDIATE_VALUE));

    const cpu::DecodedInstruction pop_mem =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8F, 0x45, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(pop_mem.instruction.opcode(), Eq(cpu::Opcode::POP));
    EXPECT_TRUE(pop_mem.instruction.first_operand().is_memory());
    EXPECT_THAT(pop_mem.instruction.first_operand().memory_base_register(), Eq(cpu::RegisterId::RBP));

    const cpu::DecodedInstruction call_rel32 =
        decoder.decode(cpu::ExecutableImage{0xE8, 0x05, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(call_rel32.instruction.opcode(), Eq(cpu::Opcode::CALL));
    EXPECT_THAT(call_rel32.instruction.first_operand().immediate_value(), Eq(cpu::SignedQword{10}));

    const cpu::DecodedInstruction ret = decoder.decode(cpu::ExecutableImage{0xC3}, TEST_RIP_ZERO);
    EXPECT_THAT(ret.instruction.opcode(), Eq(cpu::Opcode::RET));

    const cpu::DecodedInstruction call_rax =
        decoder.decode(cpu::ExecutableImage{0x48, 0xFF, 0xD0}, TEST_RIP_ZERO);
    EXPECT_THAT(call_rax.instruction.opcode(), Eq(cpu::Opcode::CALL));
    EXPECT_THAT(call_rax.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction push_mem =
        decoder.decode(cpu::ExecutableImage{0x48, 0xFF, 0x75, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(push_mem.instruction.opcode(), Eq(cpu::Opcode::PUSH));
    EXPECT_TRUE(push_mem.instruction.first_operand().is_memory());

    const cpu::DecodedInstruction lea =
        decoder.decode(cpu::ExecutableImage{0x48, 0x8D, 0x44, 0x8D, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(lea.instruction.opcode(), Eq(cpu::Opcode::LEA));
    EXPECT_THAT(lea.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(lea.instruction.second_operand().memory_base_register(), Eq(cpu::RegisterId::RBP));
    EXPECT_THAT(lea.instruction.second_operand().memory_index_register(), Eq(cpu::RegisterId::RCX));
    EXPECT_THAT(lea.instruction.second_operand().memory_scale(), Eq(cpu::MEMORY_ADDRESS_SCALE_4));
}

TEST(DecoderTest, DecodesLogicalIncDecAndExtensionLoadForms)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction and_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x21, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(and_reg.instruction.opcode(), Eq(cpu::Opcode::AND));
    EXPECT_THAT(and_reg.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction or_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x0B, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(or_reg.instruction.opcode(), Eq(cpu::Opcode::OR));
    EXPECT_THAT(or_reg.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));

    const cpu::DecodedInstruction or_imm =
        decoder.decode(cpu::ExecutableImage{0x48, 0x81, 0xC8, 0x2A, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(or_imm.instruction.opcode(), Eq(cpu::Opcode::OR));
    EXPECT_THAT(or_imm.instruction.second_operand().immediate_value(), Eq(TEST_IMMEDIATE_VALUE));

    const cpu::DecodedInstruction xor_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x33, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(xor_reg.instruction.opcode(), Eq(cpu::Opcode::XOR));
    EXPECT_THAT(xor_reg.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));

    const cpu::DecodedInstruction xor_imm =
        decoder.decode(cpu::ExecutableImage{0x48, 0x81, 0xF0, 0x0F, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(xor_imm.instruction.opcode(), Eq(cpu::Opcode::XOR));

    const cpu::DecodedInstruction test_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x85, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(test_reg.instruction.opcode(), Eq(cpu::Opcode::TEST));

    const cpu::DecodedInstruction test_imm =
        decoder.decode(cpu::ExecutableImage{0x48, 0xF7, 0xC0, 0x2A, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(test_imm.instruction.opcode(), Eq(cpu::Opcode::TEST));
    EXPECT_THAT(test_imm.instruction.second_operand().immediate_value(), Eq(TEST_IMMEDIATE_VALUE));

    const cpu::DecodedInstruction inc_mem =
        decoder.decode(cpu::ExecutableImage{0x48, 0xFF, 0x45, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(inc_mem.instruction.opcode(), Eq(cpu::Opcode::INC));
    EXPECT_TRUE(inc_mem.instruction.first_operand().is_memory());

    const cpu::DecodedInstruction dec_rax =
        decoder.decode(cpu::ExecutableImage{0x48, 0xFF, 0xC8}, TEST_RIP_ZERO);
    EXPECT_THAT(dec_rax.instruction.opcode(), Eq(cpu::Opcode::DEC));
    EXPECT_THAT(dec_rax.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));

    const cpu::DecodedInstruction movzx_byte =
        decoder.decode(cpu::ExecutableImage{0x48, 0x0F, 0xB6, 0x45, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(movzx_byte.instruction.opcode(), Eq(cpu::Opcode::MOVZX));
    EXPECT_THAT(movzx_byte.instruction.second_operand().memory_data_size(), Eq(cpu::DataSize::BYTE));

    const cpu::DecodedInstruction movsx_word =
        decoder.decode(cpu::ExecutableImage{0x48, 0x0F, 0xBF, 0x5D, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(movsx_word.instruction.opcode(), Eq(cpu::Opcode::MOVSX));
    EXPECT_THAT(movsx_word.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RBX));
    EXPECT_THAT(movsx_word.instruction.second_operand().memory_data_size(), Eq(cpu::DataSize::WORD));

    const cpu::DecodedInstruction movsxd =
        decoder.decode(cpu::ExecutableImage{0x48, 0x63, 0x45, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(movsxd.instruction.opcode(), Eq(cpu::Opcode::MOVSX));
    EXPECT_THAT(movsxd.instruction.second_operand().memory_data_size(), Eq(cpu::DataSize::DWORD));
}

TEST(DecoderTest, DecodesConditionCodeOperations)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction jo_rel8 = decoder.decode(cpu::ExecutableImage{0x70, 0x03, 0xF4}, TEST_RIP_ZERO);
    EXPECT_THAT(jo_rel8.instruction.opcode(), Eq(cpu::Opcode::JCC));
    EXPECT_THAT(jo_rel8.instruction.condition_code(), Eq(cpu::ConditionCode::O));
    EXPECT_THAT(jo_rel8.instruction.first_operand().immediate_value(), Eq(TEST_REL8_FORWARD_TARGET));

    const cpu::DecodedInstruction jg_rel32 =
        decoder.decode(cpu::ExecutableImage{0x0F, 0x8F, 0x05, 0x00, 0x00, 0x00}, TEST_RIP_ZERO);
    EXPECT_THAT(jg_rel32.instruction.opcode(), Eq(cpu::Opcode::JCC));
    EXPECT_THAT(jg_rel32.instruction.condition_code(), Eq(cpu::ConditionCode::G));
    EXPECT_THAT(jg_rel32.instruction.first_operand().immediate_value(), Eq(TEST_REL32_FORWARD_TARGET));

    const cpu::DecodedInstruction setg_mem =
        decoder.decode(cpu::ExecutableImage{0x0F, 0x9F, 0x45, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(setg_mem.instruction.opcode(), Eq(cpu::Opcode::SETCC));
    EXPECT_THAT(setg_mem.instruction.condition_code(), Eq(cpu::ConditionCode::G));
    EXPECT_THAT(setg_mem.instruction.first_operand().memory_data_size(), Eq(cpu::DataSize::BYTE));

    const cpu::DecodedInstruction cmovl =
        decoder.decode(cpu::ExecutableImage{0x48, 0x0F, 0x4C, 0xC3}, TEST_RIP_ZERO);
    EXPECT_THAT(cmovl.instruction.opcode(), Eq(cpu::Opcode::CMOVCC));
    EXPECT_THAT(cmovl.instruction.condition_code(), Eq(cpu::ConditionCode::L));
    EXPECT_THAT(cmovl.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(cmovl.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RBX));
}

TEST(DecoderTest, DecodesStage3TrapAndSyscallForms)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction int3 = decoder.decode(cpu::ExecutableImage{0xCC}, TEST_RIP_ZERO);
    EXPECT_THAT(int3.instruction.opcode(), Eq(cpu::Opcode::INT));
    EXPECT_THAT(
        int3.instruction.first_operand().immediate_value(),
        Eq(static_cast<cpu::SignedQword>(cpu_system::InterruptVector::breakpoint().value())));

    const cpu::DecodedInstruction int_syscall = decoder.decode(cpu::ExecutableImage{0xCD, 0x80}, TEST_RIP_ZERO);
    EXPECT_THAT(int_syscall.instruction.opcode(), Eq(cpu::Opcode::INT));
    EXPECT_THAT(
        int_syscall.instruction.first_operand().immediate_value(),
        Eq(static_cast<cpu::SignedQword>(cpu_system::InterruptVector::syscall_compat().value())));

    const cpu::DecodedInstruction syscall = decoder.decode(cpu::ExecutableImage{0x0F, 0x05}, TEST_RIP_ZERO);
    EXPECT_THAT(syscall.instruction.opcode(), Eq(cpu::Opcode::SYSCALL));

    const cpu::DecodedInstruction sysret = decoder.decode(cpu::ExecutableImage{0x48, 0x0F, 0x07}, TEST_RIP_ZERO);
    EXPECT_THAT(sysret.instruction.opcode(), Eq(cpu::Opcode::SYSRET));

    const cpu::DecodedInstruction iret = decoder.decode(cpu::ExecutableImage{0xCF}, TEST_RIP_ZERO);
    EXPECT_THAT(iret.instruction.opcode(), Eq(cpu::Opcode::IRET));
}

TEST(DecoderTest, DecodesStage6AtomicAndFenceForms)
{
    cpu::Decoder decoder;

    const cpu::DecodedInstruction locked_xadd =
        decoder.decode(cpu::ExecutableImage{0xF0, 0x48, 0x0F, 0xC1, 0x45, 0x08}, TEST_RIP_ZERO);
    EXPECT_THAT(locked_xadd.instruction.opcode(), Eq(cpu::Opcode::XADD));
    EXPECT_TRUE(locked_xadd.instruction.is_locked());
    EXPECT_TRUE(locked_xadd.instruction.first_operand().is_memory());
    EXPECT_THAT(locked_xadd.instruction.first_operand().memory_base_register(), Eq(cpu::RegisterId::RBP));
    EXPECT_THAT(locked_xadd.instruction.first_operand().memory_displacement(), Eq(TEST_DISPLACEMENT_VALUE));
    EXPECT_THAT(locked_xadd.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(locked_xadd.next_rip, Eq(cpu::InstructionPointer{6}));

    const cpu::DecodedInstruction cmpxchg_reg =
        decoder.decode(cpu::ExecutableImage{0x48, 0x0F, 0xB1, 0xD8}, TEST_RIP_ZERO);
    EXPECT_THAT(cmpxchg_reg.instruction.opcode(), Eq(cpu::Opcode::CMPXCHG));
    EXPECT_FALSE(cmpxchg_reg.instruction.is_locked());
    EXPECT_THAT(cmpxchg_reg.instruction.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(cmpxchg_reg.instruction.second_operand().register_id(), Eq(cpu::RegisterId::RBX));

    const cpu::DecodedInstruction mfence =
        decoder.decode(cpu::ExecutableImage{0x0F, 0xAE, 0xF0}, TEST_RIP_ZERO);
    EXPECT_THAT(mfence.instruction.opcode(), Eq(cpu::Opcode::MFENCE));
    EXPECT_THAT(mfence.next_rip, Eq(cpu::InstructionPointer{3}));
}

TEST(DecoderTest, RejectsTruncatedUnsupportedAndMissingRexWForms)
{
    cpu::Decoder decoder;

    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x90}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x48}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x48, 0xB8}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0xB8, 0x2A}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x8D, 0x45, 0x00}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x0F, 0x44, 0xC3}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x0F, 0x07}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x0F, 0xB6, 0xC3}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0x48, 0x8D, 0xC0}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(
        static_cast<void>(decoder.decode(cpu::ExecutableImage{0x0F, 0xA0}, TEST_RIP_ZERO)),
        cpu::DecodeError);
    EXPECT_THROW(
        static_cast<void>(decoder.decode(cpu::ExecutableImage{0x48, 0x81, 0xD0, 0x01, 0x00, 0x00, 0x00}, TEST_RIP_ZERO)),
        cpu::DecodeError);
    EXPECT_THROW(
        static_cast<void>(decoder.decode(cpu::ExecutableImage{0x48, 0x8F, 0xC8}, TEST_RIP_ZERO)),
        cpu::DecodeError);
    EXPECT_THROW(
        static_cast<void>(decoder.decode(cpu::ExecutableImage{0x48, 0xFF, 0xD8}, TEST_RIP_ZERO)),
        cpu::DecodeError);
    EXPECT_THROW(static_cast<void>(decoder.decode(cpu::ExecutableImage{0xF0, 0xF4}, TEST_RIP_ZERO)), cpu::DecodeError);
    EXPECT_THROW(
        static_cast<void>(decoder.decode(cpu::ExecutableImage{0xF0, 0x48, 0x0F, 0xB1, 0xD8}, TEST_RIP_ZERO)),
        cpu::DecodeError);
    EXPECT_THROW(
        static_cast<void>(decoder.decode(cpu::ExecutableImage{0xF0, 0x0F, 0xAE, 0xF0}, TEST_RIP_ZERO)),
        cpu::DecodeError);
    EXPECT_THROW(
        static_cast<void>(decoder.decode(cpu::ExecutableImage{0x0F, 0xAE, 0xE8}, TEST_RIP_ZERO)),
        cpu::DecodeError);
}
