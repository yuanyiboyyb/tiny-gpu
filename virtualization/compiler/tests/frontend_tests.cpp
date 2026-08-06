#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "tinygpu/compiler/ast.h"
#include "tinygpu/compiler/parser.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "test failure: " << message << "\n";
        return false;
    }
    return true;
}

bool test_parse_kernel_surface()
{
    const std::string source =
        "global Args {\n"
        "  u8 scalar;\n"
        "  u8 *ptr;\n"
        "  u8 bytes[16];\n"
        "};\n"
        "u8 main() {\n"
        "  u8 x = 1;\n"
        "  u8 arr[4];\n"
        "  if (x) {\n"
        "    arr[0] = Args.scalar;\n"
        "  } else {\n"
        "    arr[0] = 0;\n"
        "  }\n"
        "  while (x) {\n"
        "    x = x - 1;\n"
        "  }\n"
        "  for (x = 0; x; x = x + 1) {\n"
        "    arr[1] = arr[0] + x;\n"
        "  }\n"
        "  return arr[1];\n"
        "}\n";

    std::vector<std::string> diagnostics;
    tinygpu::compiler::TranslationUnit unit =
        tinygpu::compiler::parse_source(source, &diagnostics);

    if (!expect(diagnostics.empty(), "unexpected diagnostics")) {
        return false;
    }
    if (!expect(unit.globals.size() == 1, "expected one global declaration")) {
        return false;
    }
    if (!expect(unit.main_function != nullptr, "expected main function")) {
        return false;
    }
    if (!expect(unit.globals[0].fields.size() == 3, "expected three global fields")) {
        return false;
    }
    if (!expect(unit.main_function->body.size() == 6, "expected six statements")) {
        return false;
    }

    const std::string dump = tinygpu::compiler::dump_ast(unit);
    if (!expect(dump.find("Global(Args)") != std::string::npos,
                "dump should mention global")) {
        return false;
    }
    if (!expect(dump.find("Function(u8 main)") != std::string::npos,
                "dump should mention main")) {
        return false;
    }
    if (!expect(dump.find("If") != std::string::npos,
                "dump should mention if")) {
        return false;
    }
    if (!expect(dump.find("While") != std::string::npos,
                "dump should mention while")) {
        return false;
    }
    if (!expect(dump.find("For") != std::string::npos,
                "dump should mention for")) {
        return false;
    }
    return true;
}

bool test_reject_non_main_function()
{
    const std::string source =
        "u8 helper() {\n"
        "  return 0;\n"
        "}\n";

    std::vector<std::string> diagnostics;
    tinygpu::compiler::TranslationUnit unit =
        tinygpu::compiler::parse_source(source, &diagnostics);

    (void)unit;
    return expect(!diagnostics.empty(), "expected diagnostics for non-main function");
}

}  // namespace

int main()
{
    if (!test_parse_kernel_surface()) {
        return EXIT_FAILURE;
    }
    if (!test_reject_non_main_function()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
