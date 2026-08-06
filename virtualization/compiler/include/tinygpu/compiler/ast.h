#ifndef TINYGPU_COMPILER_AST_H
#define TINYGPU_COMPILER_AST_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tinygpu::compiler {

enum class TypeKind {
    U8,
    Pointer,
    Array,
};

struct Type {
    TypeKind kind = TypeKind::U8;
    std::unique_ptr<Type> element;
    std::uint32_t array_size = 0;

    static Type make_u8();
    static Type make_pointer(Type pointee);
    static Type make_array(Type element_type, std::uint32_t size);
};

enum class UnaryOp {
    AddressOf,
    Dereference,
    Plus,
    Minus,
};

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
};

struct Expr {
    virtual ~Expr() = default;
};

struct NumberExpr final : Expr {
    std::uint32_t value = 0;
};

struct IdentifierExpr final : Expr {
    std::string name;
};

struct UnaryExpr final : Expr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr final : Expr {
    BinaryOp op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

struct IndexExpr final : Expr {
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
};

struct MemberExpr final : Expr {
    std::unique_ptr<Expr> base;
    std::string member;
};

struct Stmt {
    virtual ~Stmt() = default;
};

struct BlockStmt;

struct ExprStmt final : Stmt {
    std::unique_ptr<Expr> expr;
};

struct ReturnStmt final : Stmt {
    std::unique_ptr<Expr> value;
};

struct AssignStmt final : Stmt {
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

struct VarDecl final : Stmt {
    Type type;
    std::string name;
    std::unique_ptr<Expr> init;
};

struct BlockStmt final : Stmt {
    std::vector<std::unique_ptr<Stmt>> body;
};

struct IfStmt final : Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> then_block;
    std::unique_ptr<BlockStmt> else_block;
};

struct WhileStmt final : Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> body;
};

struct ForStmt final : Stmt {
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> step;
    std::unique_ptr<BlockStmt> body;
};

struct FieldDecl {
    Type type;
    std::string name;
};

struct GlobalDecl {
    std::string name;
    std::vector<FieldDecl> fields;
};

struct FunctionDecl {
    Type return_type;
    std::string name;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct TranslationUnit {
    std::vector<GlobalDecl> globals;
    std::unique_ptr<FunctionDecl> main_function;
};

std::string dump_ast(const TranslationUnit &unit);

}  // namespace tinygpu::compiler

#endif
