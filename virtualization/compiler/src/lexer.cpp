#include "tinygpu/compiler/lexer.h"

#include <cctype>
#include <sstream>
#include <utility>

namespace tinygpu::compiler {

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

namespace {

bool is_identifier_start(char ch)
{
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_identifier_continue(char ch)
{
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

Token make_simple(TokenKind kind, std::size_t offset, char ch)
{
    Token token;
    token.kind = kind;
    token.offset = offset;
    token.lexeme.assign(1, ch);
    return token;
}

}  // namespace

std::vector<Token> Lexer::lex(std::vector<std::string> *diagnostics) const
{
    std::vector<Token> tokens;
    std::size_t i = 0;

    while (i < source_.size()) {
        const char ch = source_[i];
        if (std::isspace(static_cast<unsigned char>(ch))) {
            ++i;
            continue;
        }

        if (ch == '/' && i + 1 < source_.size() && source_[i + 1] == '/') {
            i += 2;
            while (i < source_.size() && source_[i] != '\n') {
                ++i;
            }
            continue;
        }

        if (is_identifier_start(ch)) {
            const std::size_t start = i;
            ++i;
            while (i < source_.size() && is_identifier_continue(source_[i])) {
                ++i;
            }

            Token token;
            token.kind = TokenKind::Identifier;
            token.offset = start;
            token.lexeme = source_.substr(start, i - start);
            if (token.lexeme == "global") {
                token.kind = TokenKind::KwGlobal;
            } else if (token.lexeme == "else") {
                token.kind = TokenKind::KwElse;
            } else if (token.lexeme == "for") {
                token.kind = TokenKind::KwFor;
            } else if (token.lexeme == "if") {
                token.kind = TokenKind::KwIf;
            } else if (token.lexeme == "return") {
                token.kind = TokenKind::KwReturn;
            } else if (token.lexeme == "u8") {
                token.kind = TokenKind::KwU8;
            } else if (token.lexeme == "while") {
                token.kind = TokenKind::KwWhile;
            }
            tokens.push_back(std::move(token));
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch))) {
            const std::size_t start = i;
            std::uint32_t value = 0;
            while (i < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[i]))) {
                value = value * 10u +
                        static_cast<std::uint32_t>(source_[i] - '0');
                ++i;
            }
            Token token;
            token.kind = TokenKind::Number;
            token.offset = start;
            token.lexeme = source_.substr(start, i - start);
            token.number = value;
            tokens.push_back(std::move(token));
            continue;
        }

        switch (ch) {
        case '(':
            tokens.push_back(make_simple(TokenKind::LParen, i++, ch));
            break;
        case ')':
            tokens.push_back(make_simple(TokenKind::RParen, i++, ch));
            break;
        case '{':
            tokens.push_back(make_simple(TokenKind::LBrace, i++, ch));
            break;
        case '}':
            tokens.push_back(make_simple(TokenKind::RBrace, i++, ch));
            break;
        case '[':
            tokens.push_back(make_simple(TokenKind::LBracket, i++, ch));
            break;
        case ']':
            tokens.push_back(make_simple(TokenKind::RBracket, i++, ch));
            break;
        case ';':
            tokens.push_back(make_simple(TokenKind::Semicolon, i++, ch));
            break;
        case ',':
            tokens.push_back(make_simple(TokenKind::Comma, i++, ch));
            break;
        case '.':
            tokens.push_back(make_simple(TokenKind::Dot, i++, ch));
            break;
        case '+':
            tokens.push_back(make_simple(TokenKind::Plus, i++, ch));
            break;
        case '-':
            tokens.push_back(make_simple(TokenKind::Minus, i++, ch));
            break;
        case '*':
            tokens.push_back(make_simple(TokenKind::Star, i++, ch));
            break;
        case '/':
            tokens.push_back(make_simple(TokenKind::Slash, i++, ch));
            break;
        case '&':
            tokens.push_back(make_simple(TokenKind::Ampersand, i++, ch));
            break;
        case '=':
            tokens.push_back(make_simple(TokenKind::Equal, i++, ch));
            break;
        default:
            if (diagnostics) {
                std::ostringstream oss;
                oss << "unexpected character '" << ch << "' at offset " << i;
                diagnostics->push_back(oss.str());
            }
            ++i;
            break;
        }
    }

    Token eof;
    eof.kind = TokenKind::EndOfFile;
    eof.offset = source_.size();
    tokens.push_back(std::move(eof));
    return tokens;
}

std::string token_kind_name(TokenKind kind)
{
    switch (kind) {
    case TokenKind::EndOfFile:
        return "eof";
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::Number:
        return "number";
    case TokenKind::KwGlobal:
        return "global";
    case TokenKind::KwElse:
        return "else";
    case TokenKind::KwFor:
        return "for";
    case TokenKind::KwIf:
        return "if";
    case TokenKind::KwReturn:
        return "return";
    case TokenKind::KwU8:
        return "u8";
    case TokenKind::KwWhile:
        return "while";
    case TokenKind::LParen:
        return "(";
    case TokenKind::RParen:
        return ")";
    case TokenKind::LBrace:
        return "{";
    case TokenKind::RBrace:
        return "}";
    case TokenKind::LBracket:
        return "[";
    case TokenKind::RBracket:
        return "]";
    case TokenKind::Semicolon:
        return ";";
    case TokenKind::Comma:
        return ",";
    case TokenKind::Dot:
        return ".";
    case TokenKind::Plus:
        return "+";
    case TokenKind::Minus:
        return "-";
    case TokenKind::Star:
        return "*";
    case TokenKind::Slash:
        return "/";
    case TokenKind::Ampersand:
        return "&";
    case TokenKind::Equal:
        return "=";
    }

    return "?";
}

}  // namespace tinygpu::compiler
