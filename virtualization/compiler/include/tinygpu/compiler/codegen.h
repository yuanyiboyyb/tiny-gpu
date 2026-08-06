#ifndef TINYGPU_COMPILER_CODEGEN_H
#define TINYGPU_COMPILER_CODEGEN_H

#include <string>
#include <vector>

#include "tinygpu/compiler/ast.h"

namespace tinygpu::compiler {

std::string generate_assembly(const TranslationUnit &unit,
                              std::vector<std::string> *diagnostics = nullptr);

}  // namespace tinygpu::compiler

#endif
