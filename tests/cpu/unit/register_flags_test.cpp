#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/flags/rflags.hpp>
#include <mnos/cpu/register/bank.hpp>
#include <mnos/cpu/register/id.hpp>
#include <support/test_assert.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;

namespace
{
constexpr auto TEST_INVALID_REGISTER_ID = static_cast<cpu::RegisterId>(cpu::REGISTER_ID_GENERAL_REGISTER_COUNT);
constexpr auto TEST_INVALID_FLAG_ID = static_cast<cpu::FlagId>(cpu::FLAG_ID_STATUS_FLAG_COUNT);

constexpr cpu::UQWORD64 TEST_REGISTER_VALUE = cpu::UQWORD64{0x1234ABCDULL};
constexpr cpu::UQWORD64 TEST_SECOND_REGISTER_VALUE = cpu::UQWORD64{0xFEDCBA98ULL};
constexpr cpu::UQWORD64 TEST_ONE_BIT = cpu::UQWORD64{1};
constexpr cpu::UQWORD64 TEST_QWORD_SIGN_MASK =
    TEST_ONE_BIT << (cpu::DATA_SIZE_QWORD_BITS - std::size_t{1});
constexpr cpu::UQWORD64 TEST_CF_MASK = TEST_ONE_BIT << cpu::FLAG_ID_CF_BIT_INDEX;

void test_register_ids()
{
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

    for (const RegisterCase test_case : REGISTER_CASES)
    {
        test::check(cpu::is_register_id_valid(test_case.id), "register id should be valid");
        test::check(cpu::register_id_to_index(test_case.id) == test_case.index, "register index mismatch");
        test::check(cpu::register_id_to_assembly_name(test_case.id) == test_case.assembly_name,
                    "register assembly name mismatch");
    }

    test::check(!cpu::is_register_id_valid(TEST_INVALID_REGISTER_ID), "invalid register id should be rejected");
    test::check(cpu::register_id_to_assembly_name(TEST_INVALID_REGISTER_ID) == "<invalid>",
                "invalid register name should be stable");
}

void test_register_bank()
{
    cpu::RegisterBank bank;
    test::check(bank.read(cpu::RegisterId::RAX) == cpu::UQWORD64{0}, "register bank should zero initialize");
    bank.write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    bank.write(cpu::RegisterId::R15, TEST_SECOND_REGISTER_VALUE);
    test::check(bank.read(cpu::RegisterId::RAX) == TEST_REGISTER_VALUE, "register bank RAX read mismatch");
    test::check(bank.read(cpu::RegisterId::R15) == TEST_SECOND_REGISTER_VALUE, "register bank R15 read mismatch");
    test::check_throws<std::out_of_range>(
        [&bank]() {
            static_cast<void>(bank.read(TEST_INVALID_REGISTER_ID));
        },
        "invalid register read");
    test::check_throws<std::out_of_range>(
        [&bank]() {
            bank.write(TEST_INVALID_REGISTER_ID, TEST_REGISTER_VALUE);
        },
        "invalid register write");
}

void test_flag_ids()
{
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

    for (const FlagCase test_case : FLAG_CASES)
    {
        test::check(cpu::is_flag_id_valid(test_case.id), "flag id should be valid");
        test::check(cpu::flag_id_to_index(test_case.id) == test_case.index, "flag index mismatch");
        test::check(cpu::flag_id_to_bit_index(test_case.id) == test_case.bit_index, "flag bit index mismatch");
        test::check(cpu::flag_id_to_assembly_name(test_case.id) == test_case.assembly_name,
                    "flag assembly name mismatch");
    }

    test::check(!cpu::is_flag_id_valid(TEST_INVALID_FLAG_ID), "invalid flag id should be rejected");
    test::check(cpu::flag_id_to_assembly_name(TEST_INVALID_FLAG_ID) == "<invalid>",
                "invalid flag name should be stable");
    test::check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::flag_id_to_bit_index(TEST_INVALID_FLAG_ID));
        },
        "invalid flag bit index");
}

void test_rflags()
{
    cpu::Rflags flags;
    test::check(flags.raw_bits() == cpu::UQWORD64{0}, "rflags should zero initialize");
    flags.write(cpu::FlagId::CF, true);
    test::check(flags.read(cpu::FlagId::CF), "CF should be set");
    test::check(flags.raw_bits() == TEST_CF_MASK, "CF raw bit mismatch");
    flags.write(cpu::FlagId::CF, false);
    test::check(!flags.read(cpu::FlagId::CF), "CF should be cleared");

    flags.update_zero_sign_from_qword(cpu::UQWORD64{0});
    test::check(flags.read(cpu::FlagId::ZF), "zero result should set ZF");
    test::check(!flags.read(cpu::FlagId::SF), "zero result should clear SF");

    flags.update_zero_sign_from_qword(TEST_QWORD_SIGN_MASK);
    test::check(!flags.read(cpu::FlagId::ZF), "non-zero result should clear ZF");
    test::check(flags.read(cpu::FlagId::SF), "sign bit should set SF");

    flags.clear_status_flags();
    test::check(flags.raw_bits() == cpu::UQWORD64{0}, "clear status flags should clear raw bits");
    test::check_throws<std::out_of_range>(
        [&flags]() {
            static_cast<void>(flags.read(TEST_INVALID_FLAG_ID));
        },
        "invalid flag read");
    test::check_throws<std::out_of_range>(
        [&flags]() {
            flags.write(TEST_INVALID_FLAG_ID, true);
        },
        "invalid flag write");
}
}

int main()
{
    test_register_ids();
    test_register_bank();
    test_flag_ids();
    test_rflags();

    std::cout << "mnos_cpu_register_flags_unit_tests passed\n";
    return 0;
}
