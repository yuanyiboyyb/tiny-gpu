#include "tinygpu/compiler/parser.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace tinygpu::compiler {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token &Parser::peek() const
{
    return tokens_[index_];
}

const Token &Parser::previous() const
{
    return tokens_[index_ - 1];
}

bool Parser::at_end() const
{
    return peek().kind == TokenKind::EndOfFile;
}

bool Parser::check(TokenKind kind) const
{
    return !at_end() && peek().kind == kind;
}

bool Parser::match(TokenKind kind)
{
    if (!check(kind)) {
        return false;
    }
    ++index_;
    return true;
}

const Token &Parser::consume(TokenKind kind, const std::string &message)
{
    if (check(kind)) {
        ++index_;
        return previous();
    }

    if (diagnostics_) {
        std::ostringstream oss;
        oss << message << " at offset " << peek().offset
            << ", got " << token_kind_name(peek().kind);
        diagnostics_->push_back(oss.str());
    }
    throw std::runtime_error(message);
}

TranslationUnit Parser::parse(std::vector<std::string> *diagnostics)
{
    diagnostics_ = diagnostics;
    try {
        return parse_translation_unit();
    } catch (const std::runtime_error &) {
        return TranslationUnit{};
    }
}

TranslationUnit Parser::parse_translation_unit()
{
    TranslationUnit unit;

    while (!at_end()) {
        if (match(TokenKind::KwGlobal)) {
            --index_;
            unit.globals.push_back(parse_global_decl());
        } else if (check(TokenKind::KwU8)) {
            if (unit.main_function) {
                consume(TokenKind::EndOfFile, "only one main function is supported");
            }
            auto func = std::make_unique<FunctionDecl>(parse_function_decl());
            if (func->name != "main") {
                if (diagnostics_) {
                    diagnostics_->push_back("only a single function named 'main' is supported");
                }
                throw std::runtime_error("unsupported function");
            }
            unit.main_function = std::move(func);
        } else {
            consume(TokenKind::KwU8, "expected global declaration or function");
        }
    }

    return unit;
}

GlobalDecl Parser::parse_global_decl()
{
    consume(TokenKind::KwGlobal, "expected global");
    GlobalDecl decl;
    decl.name = consume(TokenKind::Identifier, "expected global name").lexeme;
    consume(TokenKind::LBrace, "expected '{' after global name");

    while (!check(TokenKind::RBrace)) {
        FieldDecl field;
        field.type = parse_type();
        field.name = consume(TokenKind::Identifier, "expected field name").lexeme;
        if (match(TokenKind::LBracket)) {
            const Token &size = consume(TokenKind::Number, "expected array size");
            consume(TokenKind::RBracket, "expected ']'");
            field.type = Type::make_array(std::move(field.type), size.number);
        }
        consume(TokenKind::Semicolon, "expected ';' after field");
        decl.fields.push_back(std::move(field));
    }

    consume(TokenKind::RBrace, "expected '}' after global");
    consume(TokenKind::Semicolon, "expected ';' after global declaration");
    return decl;
}

FunctionDecl Parser::parse_function_decl()
{
    FunctionDecl decl;
    decl.return_type = parse_type();
    decl.name = consume(TokenKind::Identifier, "expected function name").lexeme;
    consume(TokenKind::LParen, "expected '(' after function name");
    if (!check(TokenKind::RParen)) {
        if (diagnostics_) {
            diagnostics_->push_back("main function does not support parameters");
        }
        throw std::runtime_error("unsupported parameters");
    }
    consume(TokenKind::RParen, "expected ')' after parameters");
    consume(TokenKind::LBrace, "expected '{' before function body");
    decl.body = parse_block();
    return decl;
}

std::unique_ptr<BlockStmt> Parser::parse_block_stmt()
{
    consume(TokenKind::LBrace, "expected '{'");
    auto block = std::make_unique<BlockStmt>();
    block->body = parse_block();
    return block;
}

std::vector<std::unique_ptr<Stmt>> Parser::parse_block()
{
    std::vector<std::unique_ptr<Stmt>> body;

    while (!check(TokenKind::RBrace)) {
        body.push_back(parse_stmt());
    }

    consume(TokenKind::RBrace, "expected '}' after block");
    return body;
}

std::unique_ptr<Stmt> Parser::parse_stmt()
{
    if (check(TokenKind::LBrace)) {
        return parse_block_stmt();
    }
    if (check(TokenKind::KwIf)) {
        return parse_if_stmt();
    }
    if (check(TokenKind::KwWhile)) {
        return parse_while_stmt();
    }
    if (check(TokenKind::KwFor)) {
        return parse_for_stmt();
    }
    if (check(TokenKind::KwReturn)) {
        return parse_return_stmt();
    }
    return parse_decl_or_expr_stmt();
}

std::unique_ptr<Stmt> Parser::parse_if_stmt()
{
    consume(TokenKind::KwIf, "expected if");
    consume(TokenKind::LParen, "expected '(' after if");
    auto stmt = std::make_unique<IfStmt>();
    stmt->condition = parse_expr();
    consume(TokenKind::RParen, "expected ')' after if condition");
    stmt->then_block = parse_block_stmt();
    if (match(TokenKind::KwElse)) {
        stmt->else_block = parse_block_stmt();
    }
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_while_stmt()
{
    consume(TokenKind::KwWhile, "expected while");
    consume(TokenKind::LParen, "expected '(' after while");
    auto stmt = std::make_unique<WhileStmt>();
    stmt->condition = parse_expr();
    consume(TokenKind::RParen, "expected ')' after while condition");
    stmt->body = parse_block_stmt();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_for_stmt()
{
    consume(TokenKind::KwFor, "expected for");
    consume(TokenKind::LParen, "expected '(' after for");
    auto stmt = std::make_unique<ForStmt>();
    if (!check(TokenKind::Semicolon)) {
        stmt->init = parse_decl_or_expr_stmt(false);
    }
    consume(TokenKind::Semicolon, "expected ';' after for init");
    if (!check(TokenKind::Semicolon)) {
        stmt->condition = parse_expr();
    }
    consume(TokenKind::Semicolon, "expected ';' after for condition");
    if (!check(TokenKind::RParen)) {
        stmt->step = parse_decl_or_expr_stmt(false);
    }
    consume(TokenKind::RParen, "expected ')' after for clauses");
    stmt->body = parse_block_stmt();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_return_stmt()
{
    consume(TokenKind::KwReturn, "expected return");
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->value = parse_expr();
    consume(TokenKind::Semicolon, "expected ';' after return");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_decl_or_expr_stmt(bool require_semicolon)
{
    if (check(TokenKind::KwU8)) {
        auto decl = std::make_unique<VarDecl>(parse_var_decl());
        if (require_semicolon) {
            consume(TokenKind::Semicolon, "expected ';' after variable declaration");
        }
        return decl;
    }

    auto target = parse_expr();
    if (match(TokenKind::Equal)) {
        auto stmt = std::make_unique<AssignStmt>();
        stmt->target = std::move(target);
        stmt->value = parse_expr();
        if (require_semicolon) {
            consume(TokenKind::Semicolon, "expected ';' after assignment");
        }
        return stmt;
    }

    auto stmt = std::make_unique<ExprStmt>();
    stmt->expr = std::move(target);
    if (require_semicolon) {
        consume(TokenKind::Semicolon, "expected ';' after expression");
    }
    return stmt;
}

VarDecl Parser::parse_var_decl()
{
    VarDecl decl;
    decl.type = parse_type();
    decl.name = consume(TokenKind::Identifier, "expected variable name").lexeme;
    if (match(TokenKind::LBracket)) {
        const Token &size = consume(TokenKind::Number, "expected array size");
        consume(TokenKind::RBracket, "expected ']'");
        decl.type = Type::make_array(std::move(decl.type), size.number);
    }
    if (match(TokenKind::Equal)) {
        decl.init = parse_expr();
    }
    return decl;
}

Type Parser::parse_type()
{
    consume(TokenKind::KwU8, "expected type");
    Type type = Type::make_u8();
    while (match(TokenKind::Star)) {
        type = Type::make_pointer(std::move(type));
    }
    return type;
}

std::unique_ptr<Expr> Parser::parse_expr()
{
    return parse_additive();
}

std::unique_ptr<Expr> Parser::parse_additive()
{
    auto expr = parse_multiplicative();

    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        const TokenKind kind = peek().kind;
        ++index_;
        auto rhs = parse_multiplicative();
        auto binary = std::make_unique<BinaryExpr>();
        binary->op = (kind == TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Subtract;
        binary->lhs = std::move(expr);
        binary->rhs = std::move(rhs);
        expr = std::move(binary);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_multiplicative()
{
    auto expr = parse_unary();

    while (check(TokenKind::Star) || check(TokenKind::Slash)) {
        const TokenKind kind = peek().kind;
        ++index_;
        auto rhs = parse_unary();
        auto binary = std::make_unique<BinaryExpr>();
        binary->op = (kind == TokenKind::Star) ? BinaryOp::Multiply : BinaryOp::Divide;
        binary->lhs = std::move(expr);
        binary->rhs = std::move(rhs);
        expr = std::move(binary);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_unary()
{
    if (match(TokenKind::Ampersand)) {
        auto expr = std::make_unique<UnaryExpr>();
        expr->op = UnaryOp::AddressOf;
        expr->operand = parse_unary();
        return expr;
    }
    if (match(TokenKind::Star)) {
        auto expr = std::make_unique<UnaryExpr>();
        expr->op = UnaryOp::Dereference;
        expr->operand = parse_unary();
        return expr;
    }
    if (match(TokenKind::Plus)) {
        auto expr = std::make_unique<UnaryExpr>();
        expr->op = UnaryOp::Plus;
        expr->operand = parse_unary();
        return expr;
    }
    if (match(TokenKind::Minus)) {
        auto expr = std::make_unique<UnaryExpr>();
        expr->op = UnaryOp::Minus;
        expr->operand = parse_unary();
        return expr;
    }

    return parse_postfix();
}

std::unique_ptr<Expr> Parser::parse_postfix()
{
    auto expr = parse_primary();

    for (;;) {
        if (match(TokenKind::LBracket)) {
            auto index = std::make_unique<IndexExpr>();
            index->base = std::move(expr);
            index->index = parse_expr();
            consume(TokenKind::RBracket, "expected ']'");
            expr = std::move(index);
            continue;
        }
        if (match(TokenKind::Dot)) {
            auto member = std::make_unique<MemberExpr>();
            member->base = std::move(expr);
            member->member = consume(TokenKind::Identifier, "expected member name").lexeme;
            expr = std::move(member);
            continue;
        }
        break;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary()
{
    if (match(TokenKind::Number)) {
        auto expr = std::make_unique<NumberExpr>();
        expr->value = previous().number;
        return expr;
    }
    if (match(TokenKind::Identifier)) {
        auto expr = std::make_unique<IdentifierExpr>();
        expr->name = previous().lexeme;
        return expr;
    }
    if (match(TokenKind::LParen)) {
        auto expr = parse_expr();
        consume(TokenKind::RParen, "expected ')'");
        return expr;
    }

    consume(TokenKind::Identifier, "expected expression");
    return nullptr;
}

TranslationUnit parse_source(const std::string &source,
                             std::vector<std::string> *diagnostics)
{
    Lexer lexer(source);
    auto tokens = lexer.lex(diagnostics);
    Parser parser(std::move(tokens));
    return parser.parse(diagnostics);
}

}  // namespace tinygpu::compiler
