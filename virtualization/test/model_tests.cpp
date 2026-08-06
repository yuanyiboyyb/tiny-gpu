#include "tinygpu/verilated_gpu.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct KernelTest {
    std::vector<std::uint16_t> program;
    std::vector<std::uint8_t> input;
    std::vector<std::uint8_t> expected;
    std::size_t expected_offset;
    std::uint8_t thread_count;
    std::uint64_t max_cycles;
};

std::vector<std::uint8_t> run_kernel(const KernelTest& test)
{
    tinygpu::VerilatedGpu gpu;
    gpu.load_program(test.program);
    gpu.load_data(test.input.data(), test.input.size());
    gpu.set_thread_count(test.thread_count);
    gpu.start();

    EXPECT_TRUE(gpu.run_until_done(test.max_cycles))
        << "kernel did not finish within " << test.max_cycles << " cycles";

    std::vector<std::uint8_t> result(test.expected.size());
    gpu.copy_data(result.data(), result.size(), test.expected_offset);
    return result;
}

} // namespace

TEST(VerilatedGpuModel, VectorAdd)
{
    const KernelTest test = {
        {
            // Generated from ../../sim_rtl/matadd.tu.
            0x51de, // MUL   R1, R13, R14
            0x311f, // ADD   R1, R1, R15
            0x9200, // CONST R2, #0
            0x3012, // ADD   R0, R1, R2
            0x9100, // CONST R1, #0
            0x7110, // LDR   R1, R1
            0x3110, // ADD   R1, R1, R0
            0x7110, // LDR   R1, R1
            0x9201, // CONST R2, #1
            0x7220, // LDR   R2, R2
            0x3220, // ADD   R2, R2, R0
            0x7220, // LDR   R2, R2
            0x3112, // ADD   R1, R1, R2
            0x9202, // CONST R2, #2
            0x7220, // LDR   R2, R2
            0x3220, // ADD   R2, R2, R0
            0x8021, // STR   R2, R1
            0xf000, // RET
        },
        {
            3, 11, 19,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 0, 0, 0, 0, 0, 0, 0,
        },
        {0, 2, 4, 6, 8, 10, 12, 14},
        19,
        8,
        2000,
    };

    EXPECT_EQ(run_kernel(test), test.expected);
}

TEST(VerilatedGpuModel, MatrixMultiply2x2)
{
    const KernelTest test = {
        {
            0b0101000011011110, // MUL   R0, %blockIdx, %blockDim
            0b0011000000001111, // ADD   R0, R0, %threadIdx
            0b1001000100000001, // CONST R1, #1
            0b1001001000000010, // CONST R2, #2
            0b1001001100000000, // CONST R3, #0
            0b1001010000000100, // CONST R4, #4
            0b1001010100001000, // CONST R5, #8
            0b0110011000000010, // DIV   R6, R0, R2
            0b0101011101100010, // MUL   R7, R6, R2
            0b0100011100000111, // SUB   R7, R0, R7
            0b1001100000000000, // CONST R8, #0
            0b1001100100000000, // CONST R9, #0
            0b0101101001100010, // MUL   R10, R6, R2
            0b0011101010101001, // ADD   R10, R10, R9
            0b0011101010100011, // ADD   R10, R10, R3
            0b0111101010100000, // LDR   R10, R10
            0b0101101110010010, // MUL   R11, R9, R2
            0b0011101110110111, // ADD   R11, R11, R7
            0b0011101110110100, // ADD   R11, R11, R4
            0b0111101110110000, // LDR   R11, R11
            0b0101110010101011, // MUL   R12, R10, R11
            0b0011100010001100, // ADD   R8, R8, R12
            0b0011100110010001, // ADD   R9, R9, R1
            0b0010000010010010, // CMP   R9, R2
            0b0001100000011000, // BRn   LOOP at byte address 24
            0b0011100101010000, // ADD   R9, R5, R0
            0b1000000010011000, // STR   R9, R8
            0b1111000000000000, // RET
        },
        {
            1, 2, 3, 4,
            1, 2, 3, 4,
        },
        {7, 10, 15, 22},
        8,
        4,
        4000,
    };

    EXPECT_EQ(run_kernel(test), test.expected);
}

TEST(VerilatedGpuModel, MatrixMultiplyBranchJoin)
{
    const KernelTest test = {
        {
            0b0101000011011110, // MUL   R0, %blockIdx, %blockDim
            0b0011000000001111, // ADD   R0, R0, %threadIdx
            0b1001000100000001, // CONST R1, #1
            0b1001001000000010, // CONST R2, #2
            0b1001001100000000, // CONST R3, #0
            0b1001010000000100, // CONST R4, #4
            0b1001010100001000, // CONST R5, #8
            0b0110011000000010, // DIV   R6, R0, R2
            0b0101011101100010, // MUL   R7, R6, R2
            0b0100011100000111, // SUB   R7, R0, R7
            0b1001100000000000, // CONST R8, #0
            0b1001100100000000, // CONST R9, #0
            0b0101101001100010, // MUL   R10, R6, R2
            0b0011101010101001, // ADD   R10, R10, R9
            0b0011101010100011, // ADD   R10, R10, R3
            0b0111101010100000, // LDR   R10, R10
            0b0101101110010010, // MUL   R11, R9, R2
            0b0011101110110111, // ADD   R11, R11, R7
            0b0011101110110100, // ADD   R11, R11, R4
            0b0111101110110000, // LDR   R11, R11
            0b0101110010101011, // MUL   R12, R10, R11
            0b0011100010001100, // ADD   R8, R8, R12
            0b0011100110010001, // ADD   R9, R9, R1
            0b0010000010010010, // CMP   R9, R2
            0b0001100000011000, // BRn   LOOP at byte address 24
            0b1001101000001010, // CONST R10, #10
            0b0010000010001010, // CMP   R8, R10
            0b0001001000111010, // BRp   byte address 58
            0b0001111000111100, // BRnzp byte address 60
            0b1001100000000000, // CONST R8, #0
            0b1010000000000000, // JOIN
            0b0011100101010000, // ADD   R9, R5, R0
            0b1000000010011000, // STR   R9, R8
            0b1111000000000000, // RET
        },
        {
            1, 2, 3, 4,
            1, 2, 3, 4,
        },
        {7, 10, 0, 0},
        8,
        4,
        4000,
    };

    EXPECT_EQ(run_kernel(test), test.expected);
}
