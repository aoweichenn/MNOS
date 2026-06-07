#include <array>
#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/flags/rflags.hpp>
#include <mnos/cpu/register/bank.hpp>
#include <mnos/cpu/register/id.hpp>

namespace cpu = mnos::cpu;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_REGISTER_ID = static_cast<cpu::RegisterId>(cpu::REGISTER_ID_GENERAL_REGISTER_COUNT);
constexpr auto TEST_INVALID_FLAG_ID = static_cast<cpu::FlagId>(cpu::FLAG_ID_STATUS_FLAG_COUNT);

constexpr cpu::UQWORD64 TEST_REGISTER_VALUE = cpu::UQWORD64{0x1234ABCDULL};
constexpr cpu::UQWORD64 TEST_SECOND_REGISTER_VALUE = cpu::UQWORD64{0xFEDCBA98ULL};
constexpr cpu::UQWORD64 TEST_ONE_BIT = cpu::UQWORD64{1};
constexpr cpu::UQWORD64 TEST_QWORD_SIGN_MASK = TEST_ONE_BIT << (cpu::DATA_SIZE_QWORD_BITS - std::size_t{1});
constexpr cpu::UQWORD64 TEST_CF_MASK = TEST_ONE_BIT << cpu::FLAG_ID_CF_BIT_INDEX;

struct RegisterCase
{
    cpu::RegisterId id;
    std::size_t index;
    std::string_view assembly_name;
};

constexpr std::array<RegisterCase, cpu::REGISTER_ID_GENERAL_REGISTER_COUNT> REGISTER_CASES{
    RegisterCase{cpu::RegisterId::RAX, 0, "RAX"},   RegisterCase{cpu::RegisterId::RBX, 1, "RBX"},
    RegisterCase{cpu::RegisterId::RCX, 2, "RCX"},   RegisterCase{cpu::RegisterId::RDX, 3, "RDX"},
    RegisterCase{cpu::RegisterId::RSI, 4, "RSI"},   RegisterCase{cpu::RegisterId::RDI, 5, "RDI"},
    RegisterCase{cpu::RegisterId::RBP, 6, "RBP"},   RegisterCase{cpu::RegisterId::RSP, 7, "RSP"},
    RegisterCase{cpu::RegisterId::R8, 8, "R8"},     RegisterCase{cpu::RegisterId::R9, 9, "R9"},
    RegisterCase{cpu::RegisterId::R10, 10, "R10"},  RegisterCase{cpu::RegisterId::R11, 11, "R11"},
    RegisterCase{cpu::RegisterId::R12, 12, "R12"},  RegisterCase{cpu::RegisterId::R13, 13, "R13"},
    RegisterCase{cpu::RegisterId::R14, 14, "R14"},  RegisterCase{cpu::RegisterId::R15, 15, "R15"}};

struct FlagCase
{
    cpu::FlagId id;
    std::size_t index;
    std::size_t bit_index;
    std::string_view assembly_name;
};

constexpr std::array<FlagCase, cpu::FLAG_ID_STATUS_FLAG_COUNT> FLAG_CASES{
    FlagCase{cpu::FlagId::CF, 0, cpu::FLAG_ID_CF_BIT_INDEX, "CF"},
    FlagCase{cpu::FlagId::ZF, 1, cpu::FLAG_ID_ZF_BIT_INDEX, "ZF"},
    FlagCase{cpu::FlagId::SF, 2, cpu::FLAG_ID_SF_BIT_INDEX, "SF"},
    FlagCase{cpu::FlagId::OF, 3, cpu::FLAG_ID_OF_BIT_INDEX, "OF"}};
}

TEST(RegisterIdTest, CatalogMapsIdsToIndicesAndNames)
{
    for (const RegisterCase test_case : REGISTER_CASES)
    {
        EXPECT_TRUE(cpu::is_register_id_valid(test_case.id));
        EXPECT_THAT(cpu::register_id_to_index(test_case.id), Eq(test_case.index));
        EXPECT_THAT(cpu::register_id_to_assembly_name(test_case.id), Eq(test_case.assembly_name));
    }

    EXPECT_FALSE(cpu::is_register_id_valid(TEST_INVALID_REGISTER_ID));
    EXPECT_THAT(cpu::register_id_to_assembly_name(TEST_INVALID_REGISTER_ID), Eq(std::string_view{"<invalid>"}));
}

TEST(RegisterBankTest, ReadsWritesAndRejectsInvalidIds)
{
    cpu::RegisterBank bank;
    EXPECT_THAT(bank.read(cpu::RegisterId::RAX), Eq(cpu::UQWORD64{0}));

    bank.write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    bank.write(cpu::RegisterId::R15, TEST_SECOND_REGISTER_VALUE);

    EXPECT_THAT(bank.read(cpu::RegisterId::RAX), Eq(TEST_REGISTER_VALUE));
    EXPECT_THAT(bank.read(cpu::RegisterId::R15), Eq(TEST_SECOND_REGISTER_VALUE));
    EXPECT_THROW(static_cast<void>(bank.read(TEST_INVALID_REGISTER_ID)), std::out_of_range);
    EXPECT_THROW(bank.write(TEST_INVALID_REGISTER_ID, TEST_REGISTER_VALUE), std::out_of_range);
}

TEST(FlagIdTest, CatalogMapsIdsToIndicesBitsAndNames)
{
    for (const FlagCase test_case : FLAG_CASES)
    {
        EXPECT_TRUE(cpu::is_flag_id_valid(test_case.id));
        EXPECT_THAT(cpu::flag_id_to_index(test_case.id), Eq(test_case.index));
        EXPECT_THAT(cpu::flag_id_to_bit_index(test_case.id), Eq(test_case.bit_index));
        EXPECT_THAT(cpu::flag_id_to_assembly_name(test_case.id), Eq(test_case.assembly_name));
    }

    EXPECT_FALSE(cpu::is_flag_id_valid(TEST_INVALID_FLAG_ID));
    EXPECT_THAT(cpu::flag_id_to_assembly_name(TEST_INVALID_FLAG_ID), Eq(std::string_view{"<invalid>"}));
    EXPECT_THROW(static_cast<void>(cpu::flag_id_to_bit_index(TEST_INVALID_FLAG_ID)), std::out_of_range);
}

TEST(RflagsTest, StoresStatusBitsAndUpdatesZeroSignFlags)
{
    cpu::Rflags flags;
    EXPECT_THAT(flags.raw_bits(), Eq(cpu::UQWORD64{0}));

    flags.write(cpu::FlagId::CF, true);
    EXPECT_TRUE(flags.read(cpu::FlagId::CF));
    EXPECT_THAT(flags.raw_bits(), Eq(TEST_CF_MASK));

    flags.write(cpu::FlagId::CF, false);
    EXPECT_FALSE(flags.read(cpu::FlagId::CF));

    flags.update_zero_sign_from_qword(cpu::UQWORD64{0});
    EXPECT_TRUE(flags.read(cpu::FlagId::ZF));
    EXPECT_FALSE(flags.read(cpu::FlagId::SF));

    flags.update_zero_sign_from_qword(TEST_QWORD_SIGN_MASK);
    EXPECT_FALSE(flags.read(cpu::FlagId::ZF));
    EXPECT_TRUE(flags.read(cpu::FlagId::SF));

    flags.clear_status_flags();
    EXPECT_THAT(flags.raw_bits(), Eq(cpu::UQWORD64{0}));
    EXPECT_THROW(static_cast<void>(flags.read(TEST_INVALID_FLAG_ID)), std::out_of_range);
    EXPECT_THROW(flags.write(TEST_INVALID_FLAG_ID, true), std::out_of_range);
}
