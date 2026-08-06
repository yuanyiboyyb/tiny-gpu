#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "tinygpu/compiler/assembler.h"

int main(int argc, char **argv)
{
    if (argc != 4 || std::string(argv[2]) != "-o") {
        std::cerr << "usage: tinygpu-as <input.s> -o <output.bin>\n";
        return 1;
    }

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "failed to open " << argv[1] << "\n";
        return 1;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    std::vector<std::string> diagnostics;
    const auto result = tinygpu::compiler::assemble(source, &diagnostics);
    if (!diagnostics.empty()) {
        for (const auto &message : diagnostics) {
            std::cerr << message << "\n";
        }
        return 1;
    }

    const auto bytes = result.program_bytes();
    std::ofstream output(argv[3], std::ios::binary);
    if (!output) {
        std::cerr << "failed to create " << argv[3] << "\n";
        return 1;
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        std::cerr << "failed to write " << argv[3] << "\n";
        return 1;
    }
    return 0;
}
