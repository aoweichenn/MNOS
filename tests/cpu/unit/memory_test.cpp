#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <support/test_assert.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;

namespace
{
constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_KIND_COUNT);

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr std::size_t TEST_MEMORY_RESIZED_SIZE_BYTES = 32;
constexpr std::size_t TEST_SINGLE_BYTE_COUNT = 1;
constexpr std::size_t TEST_TWO_BYTE_COUNT = 2;
constexpr cpu::ADDRESS64 TEST_MEMORY_BASE_ADDRESS = cpu::ADDRESS64{16};
constexpr cpu::ADDRESS64 TEST_MEMORY_SECOND_ADDRESS = cpu::ADDRESS64{24};
constexpr cpu::UQWORD64 TEST_MEMORY_BYTE_SOURCE_VALUE = cpu::UQWORD64{0x1234};
constexpr cpu::UBYTE8 TEST_MEMORY_BYTE_EXPECTED_VALUE = cpu::UBYTE8{0x34};
constexpr cpu::UWORD16 TEST_MEMORY_WORD_VALUE = cpu::UWORD16{0xABCD};
constexpr cpu::UDWORD32 TEST_MEMORY_DWORD_VALUE = cpu::UDWORD32{0xAABBCCDD};
constexpr cpu::UQWORD64 TEST_MEMORY_QWORD_VALUE = cpu::UQWORD64{0x1122334455667788ULL};
constexpr cpu::UBYTE8 TEST_MEMORY_LOW_BYTE_VALUE = cpu::UBYTE8{0x88};
constexpr cpu::UBYTE8 TEST_MEMORY_HIGH_BYTE_VALUE = cpu::UBYTE8{0x11};
constexpr cpu::UBYTE8 TEST_MEMORY_FILL_VALUE = cpu::UBYTE8{0xA5};
constexpr cpu::UBYTE8 TEST_MEMORY_INITIALIZER_FIRST_BYTE = cpu::UBYTE8{0x12};
constexpr cpu::UBYTE8 TEST_MEMORY_INITIALIZER_SECOND_BYTE = cpu::UBYTE8{0x34};

void test_physical_memory_container()
{
    cpu::PhysicalMemory memory;
    test::check(memory.empty(), "physical memory should start empty");
    memory.resize(TEST_MEMORY_RESIZED_SIZE_BYTES);
    test::check(!memory.empty(), "physical memory resize should allocate bytes");
    test::check(memory.size() == TEST_MEMORY_RESIZED_SIZE_BYTES, "physical memory resized size mismatch");
    test::check(memory.contains_range(TEST_MEMORY_BASE_ADDRESS, cpu::DATA_SIZE_QWORD_BYTES),
                "physical memory should contain qword range");
    test::check(!memory.contains_range(TEST_MEMORY_RESIZED_SIZE_BYTES, cpu::DATA_SIZE_BYTE_BYTES),
                "physical memory should reject range at end");
    test::check(!memory.contains_range(TEST_MEMORY_RESIZED_SIZE_BYTES + TEST_SINGLE_BYTE_COUNT, std::size_t{0}),
                "physical memory should reject start after end");

    const cpu::PhysicalMemory initialized_memory{
        TEST_MEMORY_INITIALIZER_FIRST_BYTE,
        TEST_MEMORY_INITIALIZER_SECOND_BYTE,
    };
    test::check(initialized_memory.size() == TEST_TWO_BYTE_COUNT, "initializer-list memory size mismatch");
    test::check(initialized_memory.read_byte(cpu::ADDRESS64{0}) == TEST_MEMORY_INITIALIZER_FIRST_BYTE,
                "initializer-list memory first byte mismatch");

    std::vector<cpu::UBYTE8> raw_bytes;
    raw_bytes.push_back(TEST_MEMORY_INITIALIZER_SECOND_BYTE);
    const cpu::PhysicalMemory vector_memory{std::move(raw_bytes)};
    test::check(vector_memory.size() == TEST_SINGLE_BYTE_COUNT, "vector memory size mismatch");
    test::check(vector_memory.read_byte(cpu::ADDRESS64{0}) == TEST_MEMORY_INITIALIZER_SECOND_BYTE,
                "vector memory byte mismatch");

    memory.clear();
    test::check(memory.empty(), "physical memory clear mismatch");
}

void test_physical_memory_little_endian_io()
{
    cpu::PhysicalMemory memory(TEST_MEMORY_RESIZED_SIZE_BYTES);
    memory.write_qword(TEST_MEMORY_BASE_ADDRESS, TEST_MEMORY_QWORD_VALUE);
    test::check(memory.read_qword(TEST_MEMORY_BASE_ADDRESS) == TEST_MEMORY_QWORD_VALUE,
                "qword little-endian read mismatch");
    test::check(memory.bytes()[static_cast<std::size_t>(TEST_MEMORY_BASE_ADDRESS)] == TEST_MEMORY_LOW_BYTE_VALUE,
                "qword low byte layout mismatch");
    test::check(memory.bytes()[static_cast<std::size_t>(TEST_MEMORY_BASE_ADDRESS) + cpu::DATA_SIZE_QWORD_BYTES -
                               TEST_SINGLE_BYTE_COUNT] == TEST_MEMORY_HIGH_BYTE_VALUE,
                "qword high byte layout mismatch");

    memory.write_word(TEST_MEMORY_SECOND_ADDRESS, TEST_MEMORY_WORD_VALUE);
    test::check(memory.read_word(TEST_MEMORY_SECOND_ADDRESS) == TEST_MEMORY_WORD_VALUE, "word read mismatch");
    memory.write_dword(TEST_MEMORY_SECOND_ADDRESS, TEST_MEMORY_DWORD_VALUE);
    test::check(memory.read_dword(TEST_MEMORY_SECOND_ADDRESS) == TEST_MEMORY_DWORD_VALUE, "dword read mismatch");
    memory.write(TEST_MEMORY_SECOND_ADDRESS, cpu::DataSize::BYTE, TEST_MEMORY_BYTE_SOURCE_VALUE);
    test::check(memory.read_byte(TEST_MEMORY_SECOND_ADDRESS) == TEST_MEMORY_BYTE_EXPECTED_VALUE,
                "byte write should keep low byte");
    memory.write_byte(TEST_MEMORY_SECOND_ADDRESS, TEST_MEMORY_FILL_VALUE);
    test::check(memory.read_byte(TEST_MEMORY_SECOND_ADDRESS) == TEST_MEMORY_FILL_VALUE, "write_byte wrapper mismatch");

    const cpu::PhysicalMemory& const_memory = memory;
    test::check(const_memory.bytes().size() == memory.size(), "const physical memory span mismatch");
    memory.fill(TEST_MEMORY_FILL_VALUE);
    test::check(memory.read_byte(TEST_MEMORY_BASE_ADDRESS) == TEST_MEMORY_FILL_VALUE, "physical memory fill mismatch");
}

void test_physical_memory_errors()
{
    cpu::PhysicalMemory memory(TEST_MEMORY_RESIZED_SIZE_BYTES);
    test::check_throws<std::out_of_range>(
        [&memory]() {
            static_cast<void>(memory.read_qword(TEST_MEMORY_RESIZED_SIZE_BYTES));
        },
        "physical memory out-of-range read");
    test::check_throws<std::out_of_range>(
        [&memory]() {
            memory.write_word(TEST_MEMORY_RESIZED_SIZE_BYTES, TEST_MEMORY_WORD_VALUE);
        },
        "physical memory out-of-range write");
    test::check_throws<std::out_of_range>(
        [&memory]() {
            static_cast<void>(memory.read(TEST_MEMORY_BASE_ADDRESS, TEST_INVALID_DATA_SIZE));
        },
        "physical memory invalid data size read");
}

void test_memory_bus()
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    test::check(!memory_bus.empty(), "memory bus should expose non-empty memory");
    test::check(memory_bus.size() == TEST_MEMORY_SIZE_BYTES, "memory bus size mismatch");
    test::check(memory_bus.contains_range(TEST_MEMORY_BASE_ADDRESS, cpu::DATA_SIZE_QWORD_BYTES),
                "memory bus range check mismatch");

    memory_bus.write(TEST_MEMORY_BASE_ADDRESS, cpu::DataSize::QWORD, TEST_MEMORY_QWORD_VALUE);
    test::check(memory_bus.read(TEST_MEMORY_BASE_ADDRESS, cpu::DataSize::QWORD) == TEST_MEMORY_QWORD_VALUE,
                "memory bus read mismatch");
    test::check(memory.read_qword(TEST_MEMORY_BASE_ADDRESS) == TEST_MEMORY_QWORD_VALUE,
                "memory bus should write physical memory");
}
}

int main()
{
    test_physical_memory_container();
    test_physical_memory_little_endian_io();
    test_physical_memory_errors();
    test_memory_bus();

    std::cout << "mnos_cpu_memory_unit_tests passed\n";
    return 0;
}
