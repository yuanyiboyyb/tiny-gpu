#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "tinygpu/compiler/assembler.h"
#include "tinygpu/compiler/codegen.h"
#include "tinygpu/compiler/parser.h"

int main()
{
    const std::string source =
        "global Args { u8 value; u8 out[4]; };\n"
        "u8 main() {\n"
        "  u8 x = Args.value + 1;\n"
        "  while (x) {\n"
        "    Args.out[x] = x;\n"
        "    x = x - 1;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";
    std::vector<std::string> diagnostics;
    auto unit = tinygpu::compiler::parse_source(source, &diagnostics);
    const std::string assembly = tinygpu::compiler::generate_assembly(unit, &diagnostics);
    const auto machine = tinygpu::compiler::assemble(assembly, &diagnostics);
    if (!diagnostics.empty() || assembly.find("LDR") == std::string::npos ||
        assembly.find("STR") == std::string::npos ||
        assembly.find("BRz") == std::string::npos || machine.instructions.empty()) {
        for (const auto &message : diagnostics) {
            std::cerr << message << '\n';
        }
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
