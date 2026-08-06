#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "tinygpu/compiler/assembler.h"
#include "tinygpu/compiler/codegen.h"
#include "tinygpu/compiler/parser.h"

namespace {

bool write_text(const std::string &path, const std::string &text)
{
    std::ofstream output(path);
    output << text;
    return static_cast<bool>(output);
}

bool write_binary(const std::string &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

}  // namespace

int main(int argc, char **argv)
{
    bool assembly_only = false;
    std::string input_path;
    std::string output_path;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-S") {
            assembly_only = true;
        } else if (argument == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (input_path.empty()) {
            input_path = argument;
        } else {
            std::cerr << "unexpected argument: " << argument << "\n";
            return 1;
        }
    }
    if (input_path.empty() || output_path.empty()) {
        std::cerr << "usage: tinygpu-cc <source.tg> [-S] -o <output>\n";
        return 1;
    }

    std::ifstream input(input_path);
    if (!input) {
        std::cerr << "failed to open " << input_path << "\n";
        return 1;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    std::vector<std::string> diagnostics;
    auto unit = tinygpu::compiler::parse_source(source, &diagnostics);
    if (diagnostics.empty()) {
        const std::string assembly = tinygpu::compiler::generate_assembly(unit, &diagnostics);
        if (diagnostics.empty()) {
            if (assembly_only) {
                if (!write_text(output_path, assembly)) {
                    std::cerr << "failed to write " << output_path << "\n";
                    return 1;
                }
                return 0;
            }
            const auto machine = tinygpu::compiler::assemble(assembly, &diagnostics);
            if (diagnostics.empty()) {
                if (!write_binary(output_path, machine.program_bytes())) {
                    std::cerr << "failed to write " << output_path << "\n";
                    return 1;
                }
                return 0;
            }
        }
    }
    for (const auto &message : diagnostics) {
        std::cerr << message << "\n";
    }
    return 1;
}
