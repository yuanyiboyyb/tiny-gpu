#ifndef TINYGPU_COMPILER_ASSEMBLER_H
#define TINYGPU_COMPILER_ASSEMBLER_H

#include <cstdint>
#include <string>
#include <vector>

namespace tinygpu::compiler {

struct AssemblyResult {
    std::vector<std::uint16_t> instructions;
    std::vector<std::uint8_t> data;
    std::uint32_t thread_count = 1;

    std::vector<std::uint8_t> program_bytes() const;
};

AssemblyResult assemble(const std::string &source,
                        std::vector<std::string> *diagnostics = nullptr);

}  // namespace tinygpu::compiler

#endif
