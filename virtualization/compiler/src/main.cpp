#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "tinygpu/compiler/ast.h"
#include "tinygpu/compiler/parser.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: tinygpu-frontend <source-file>\n";
        return 1;
    }

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "failed to open " << argv[1] << "\n";
        return 1;
    }

    std::string source((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());

    std::vector<std::string> diagnostics;
    tinygpu::compiler::TranslationUnit unit =
        tinygpu::compiler::parse_source(source, &diagnostics);
    if (!diagnostics.empty()) {
        for (const auto &message : diagnostics) {
            std::cerr << message << "\n";
        }
        return 1;
    }

    std::cout << tinygpu::compiler::dump_ast(unit);
    return 0;
}
