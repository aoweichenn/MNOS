#include <iostream>

#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>

namespace cpu = mnos::cpu;

int main()
{
    const cpu::Instruction halt = cpu::Instruction::make_halt();
    std::cout << "MNOS emulator bootstrap: " << cpu::opcode_to_assembly_name(halt.opcode()) << '\n';
    return 0;
}
