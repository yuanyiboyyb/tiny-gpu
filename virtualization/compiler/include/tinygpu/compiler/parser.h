#ifndef TINYGPU_COMPILER_PARSER_H
#define TINYGPU_COMPILER_PARSER_H

#include <string>
#include <vector>

#include "tinygpu/compiler/ast.h"
#include "tinygpu/compiler/lexer.h"

namespace tinygpu::compiler {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    TranslationUnit parse(std::vector<std::string> *diagnostics);

private:
    const Token &peek() const;
    const Token &previous() const;
    bool at_end() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token &consume(TokenKind kind, const std::string &message);

    TranslationUnit parse_translation_unit();
    GlobalDecl parse_global_decl();
    FunctionDecl parse_function_decl();
    std::unique_ptr<BlockStmt> parse_block_stmt();
    std::vector<std::unique_ptr<Stmt>> parse_block();
    std::unique_ptr<Stmt> parse_stmt();
    std::unique_ptr<Stmt> parse_if_stmt();
    std::unique_ptr<Stmt> parse_while_stmt();
    std::unique_ptr<Stmt> parse_for_stmt();
    std::unique_ptr<Stmt> parse_return_stmt();
    std::unique_ptr<Stmt> parse_decl_or_expr_stmt(bool require_semicolon = true);
    VarDecl parse_var_decl();
    Type parse_type();
    std::unique_ptr<Expr> parse_expr();
    std::unique_ptr<Expr> parse_additive();
    std::unique_ptr<Expr> parse_multiplicative();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_primary();

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
    std::vector<std::string> *diagnostics_ = nullptr;
};

TranslationUnit parse_source(const std::string &source,
                             std::vector<std::string> *diagnostics);

}  // namespace tinygpu::compiler

#endif
