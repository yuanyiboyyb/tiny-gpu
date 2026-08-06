#include "tinygpu/compiler/ast.h"

#include <sstream>
#include <utility>

namespace tinygpu::compiler {

Type Type::make_u8()
{
    return Type{TypeKind::U8, nullptr, 0};
}

Type Type::make_pointer(Type pointee)
{
    Type type;
    type.kind = TypeKind::Pointer;
    type.element = std::make_unique<Type>(std::move(pointee));
    return type;
}

Type Type::make_array(Type element_type, std::uint32_t size)
{
    Type type;
    type.kind = TypeKind::Array;
    type.element = std::make_unique<Type>(std::move(element_type));
    type.array_size = size;
    return type;
}

namespace {

std::string indent(int depth)
{
    return std::string(depth * 2, ' ');
}

std::string dump_type(const Type &type)
{
    switch (type.kind) {
    case TypeKind::U8:
        return "u8";
    case TypeKind::Pointer:
        return dump_type(*type.element) + "*";
    case TypeKind::Array: {
        std::ostringstream oss;
        oss << dump_type(*type.element) << "[" << type.array_size << "]";
        return oss.str();
    }
    }

    return "<type>";
}

std::string unary_name(UnaryOp op)
{
    switch (op) {
    case UnaryOp::AddressOf:
        return "&";
    case UnaryOp::Dereference:
        return "*";
    case UnaryOp::Plus:
        return "+";
    case UnaryOp::Minus:
        return "-";
    }

    return "?";
}

std::string binary_name(BinaryOp op)
{
    switch (op) {
    case BinaryOp::Add:
        return "+";
    case BinaryOp::Subtract:
        return "-";
    case BinaryOp::Multiply:
        return "*";
    case BinaryOp::Divide:
        return "/";
    }

    return "?";
}

void dump_expr(const Expr &expr, std::ostringstream &oss, int depth)
{
    if (const auto *number = dynamic_cast<const NumberExpr *>(&expr)) {
        oss << indent(depth) << "Number(" << number->value << ")\n";
    } else if (const auto *ident = dynamic_cast<const IdentifierExpr *>(&expr)) {
        oss << indent(depth) << "Identifier(" << ident->name << ")\n";
    } else if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
        oss << indent(depth) << "Unary(" << unary_name(unary->op) << ")\n";
        dump_expr(*unary->operand, oss, depth + 1);
    } else if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
        oss << indent(depth) << "Binary(" << binary_name(binary->op) << ")\n";
        dump_expr(*binary->lhs, oss, depth + 1);
        dump_expr(*binary->rhs, oss, depth + 1);
    } else if (const auto *index = dynamic_cast<const IndexExpr *>(&expr)) {
        oss << indent(depth) << "Index\n";
        dump_expr(*index->base, oss, depth + 1);
        dump_expr(*index->index, oss, depth + 1);
    } else if (const auto *member = dynamic_cast<const MemberExpr *>(&expr)) {
        oss << indent(depth) << "Member(" << member->member << ")\n";
        dump_expr(*member->base, oss, depth + 1);
    }
}

void dump_stmt(const Stmt &stmt, std::ostringstream &oss, int depth)
{
    if (const auto *expr_stmt = dynamic_cast<const ExprStmt *>(&stmt)) {
        oss << indent(depth) << "ExprStmt\n";
        dump_expr(*expr_stmt->expr, oss, depth + 1);
    } else if (const auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
        oss << indent(depth) << "Return\n";
        dump_expr(*ret->value, oss, depth + 1);
    } else if (const auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
        oss << indent(depth) << "Assign\n";
        dump_expr(*assign->target, oss, depth + 1);
        dump_expr(*assign->value, oss, depth + 1);
    } else if (const auto *decl = dynamic_cast<const VarDecl *>(&stmt)) {
        oss << indent(depth) << "VarDecl(" << dump_type(decl->type)
            << " " << decl->name << ")\n";
        if (decl->init) {
            dump_expr(*decl->init, oss, depth + 1);
        }
    } else if (const auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
        oss << indent(depth) << "Block\n";
        for (const auto &nested : block->body) {
            dump_stmt(*nested, oss, depth + 1);
        }
    } else if (const auto *if_stmt = dynamic_cast<const IfStmt *>(&stmt)) {
        oss << indent(depth) << "If\n";
        dump_expr(*if_stmt->condition, oss, depth + 1);
        dump_stmt(*if_stmt->then_block, oss, depth + 1);
        if (if_stmt->else_block) {
            oss << indent(depth + 1) << "Else\n";
            dump_stmt(*if_stmt->else_block, oss, depth + 2);
        }
    } else if (const auto *while_stmt = dynamic_cast<const WhileStmt *>(&stmt)) {
        oss << indent(depth) << "While\n";
        dump_expr(*while_stmt->condition, oss, depth + 1);
        dump_stmt(*while_stmt->body, oss, depth + 1);
    } else if (const auto *for_stmt = dynamic_cast<const ForStmt *>(&stmt)) {
        oss << indent(depth) << "For\n";
        if (for_stmt->init) {
            dump_stmt(*for_stmt->init, oss, depth + 1);
        }
        if (for_stmt->condition) {
            dump_expr(*for_stmt->condition, oss, depth + 1);
        }
        if (for_stmt->step) {
            dump_stmt(*for_stmt->step, oss, depth + 1);
        }
        dump_stmt(*for_stmt->body, oss, depth + 1);
    }
}

}  // namespace

std::string dump_ast(const TranslationUnit &unit)
{
    std::ostringstream oss;

    for (const auto &global : unit.globals) {
        oss << "Global(" << global.name << ")\n";
        for (const auto &field : global.fields) {
            oss << indent(1) << "Field(" << dump_type(field.type)
                << " " << field.name << ")\n";
        }
    }

    if (unit.main_function) {
        const auto &func = *unit.main_function;
        oss << "Function(" << dump_type(func.return_type) << " "
            << func.name << ")\n";
        for (const auto &stmt : func.body) {
            dump_stmt(*stmt, oss, 1);
        }
    }

    return oss.str();
}

}  // namespace tinygpu::compiler
