#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "tinygpu/compiler/assembler.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "test failure: " << message << "\n";
    }
    return condition;
}

bool test_matadd_encoding()
{
    const std::string source =
        ".threads 8\n"
        ".data 1 2 3\n"
        "MUL R0, %blockIdx, %blockDim\n"
        "ADD R0, R0, %threadIdx\n"
        "CONST R1, #8\n"
        "LDR R2, R1\n"
        "STR R1, R2\n"
        "RET\n";
    std::vector<std::string> diagnostics;
    const auto result = tinygpu::compiler::assemble(source, &diagnostics);
    return expect(diagnostics.empty(), "matadd assembly diagnostics") &&
           expect(result.thread_count == 8, "thread count") &&
           expect(result.data == std::vector<std::uint8_t>({1, 2, 3}), "data") &&
           expect(result.instructions == std::vector<std::uint16_t>({
               0x50de, 0x300f, 0x9108, 0x7210, 0x8012, 0xf000,
           }), "instruction encoding") &&
           expect(result.program_bytes()[0] == 0xde && result.program_bytes()[1] == 0x50,
                  "little-endian binary");
}

bool test_label_is_absolute_byte_address()
{
    const std::string source =
        "CONST R0, 1\n"
        "loop: SUB R0, R0, R0\n"
        "CMP R0, R1\n"
        "BRn loop\n"
        "BR done\n"
        "done: RET\n";
    std::vector<std::string> diagnostics;
    const auto result = tinygpu::compiler::assemble(source, &diagnostics);
    return expect(diagnostics.empty(), "label assembly diagnostics") &&
           expect(result.instructions.size() == 6, "label instruction count") &&
           expect(result.instructions[3] == 0x1802, "loop label byte address") &&
           expect(result.instructions[4] == 0x1e0a, "forward label byte address");
}

bool test_reject_bad_program()
{
    std::vector<std::string> diagnostics;
    (void)tinygpu::compiler::assemble("CONST R13, 1\nBR missing\n", &diagnostics);
    return expect(diagnostics.size() == 2, "expected two assembler diagnostics");
}

}  // namespace

int main()
{
    return test_matadd_encoding() && test_label_is_absolute_byte_address() &&
                   test_reject_bad_program()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
