#ifndef TINYGPU_COMPILER_LEXER_H
#define TINYGPU_COMPILER_LEXER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tinygpu::compiler {

enum class TokenKind {
    EndOfFile,
    Identifier,
    Number,
    KwGlobal,
    KwElse,
    KwFor,
    KwIf,
    KwReturn,
    KwU8,
    KwWhile,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Semicolon,
    Comma,
    Dot,
    Plus,
    Minus,
    Star,
    Slash,
    Ampersand,
    Equal,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string lexeme;
    std::size_t offset = 0;
    std::uint32_t number = 0;
};

class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> lex(std::vector<std::string> *diagnostics) const;

private:
    std::string source_;
};

std::string token_kind_name(TokenKind kind);

}  // namespace tinygpu::compiler

#endif
