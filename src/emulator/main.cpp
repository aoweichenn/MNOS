#include <iostream>

#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>

int main()
{
    const mnos::Instruction halt = mnos::Instruction::make_halt();
    std::cout << "MNOS emulator bootstrap: " << mnos::opcode_to_assembly_name(halt.opcode()) << '\n';
    return 0;
}
