#ifndef TINYGPU_COMPILER_ISA_H
#define TINYGPU_COMPILER_ISA_H

#include <cstdint>

namespace tinygpu::compiler {

enum class Opcode : std::uint16_t {
    Nop = 0x0,
    Brnzp = 0x1,
    Cmp = 0x2,
    Add = 0x3,
    Sub = 0x4,
    Mul = 0x5,
    Div = 0x6,
    Ldr = 0x7,
    Str = 0x8,
    Const = 0x9,
    Join = 0xa,
    Ret = 0xf,
};

constexpr std::uint16_t encode_rrr(Opcode opcode, std::uint8_t rd,
                                   std::uint8_t rs, std::uint8_t rt)
{
    return (static_cast<std::uint16_t>(opcode) << 12) |
           (static_cast<std::uint16_t>(rd) << 8) |
           (static_cast<std::uint16_t>(rs) << 4) |
           static_cast<std::uint16_t>(rt);
}

constexpr std::uint16_t encode_const(std::uint8_t rd, std::uint8_t value)
{
    return (static_cast<std::uint16_t>(Opcode::Const) << 12) |
           (static_cast<std::uint16_t>(rd) << 8) | value;
}

constexpr std::uint16_t encode_branch(std::uint8_t nzp, std::uint8_t address)
{
    return (static_cast<std::uint16_t>(Opcode::Brnzp) << 12) |
           (static_cast<std::uint16_t>(nzp & 0x7u) << 9) | address;
}

}  // namespace tinygpu::compiler

#endif
