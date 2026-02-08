#ifndef ALCC_AST_PRINTER_H
#define ALCC_AST_PRINTER_H

#include "AST.h"
#include <iostream>

class LuaPrinter : public ASTVisitor {
public:
    int indent_level;
    std::ostream& out;

    LuaPrinter(std::ostream& o = std::cout) : indent_level(0), out(o) {}

    void print_indent();

    void visit(Block& node) override;
    void visit(Literal& node) override;
    void visit(Variable& node) override;
    void visit(BinaryExpr& node) override;
    void visit(UnaryExpr& node) override;
    void visit(FunctionCall& node) override;
    void visit(TableConstructor& node) override;
    void visit(ClosureExpr& node) override;
    void visit(Assignment& node) override;
    void visit(IfStmt& node) override;
    void visit(WhileStmt& node) override;
    void visit(RepeatStmt& node) override;
    void visit(ForNumStmt& node) override;
    void visit(ForInStmt& node) override;
    void visit(FunctionDecl& node) override;
    void visit(ReturnStmt& node) override;
    void visit(BreakStmt& node) override;
    void visit(LabelStmt& node) override;
    void visit(GotoStmt& node) override;
    void visit(ExprStmt& node) override;
};

#endif
