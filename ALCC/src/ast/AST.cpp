#include "AST.h"

void Block::accept(ASTVisitor& v) { v.visit(*this); }
void Literal::accept(ASTVisitor& v) { v.visit(*this); }
void Variable::accept(ASTVisitor& v) { v.visit(*this); }
void BinaryExpr::accept(ASTVisitor& v) { v.visit(*this); }
void UnaryExpr::accept(ASTVisitor& v) { v.visit(*this); }
void FunctionCall::accept(ASTVisitor& v) { v.visit(*this); }
void TableConstructor::accept(ASTVisitor& v) { v.visit(*this); }
void ClosureExpr::accept(ASTVisitor& v) { v.visit(*this); }
void Assignment::accept(ASTVisitor& v) { v.visit(*this); }
void IfStmt::accept(ASTVisitor& v) { v.visit(*this); }
void WhileStmt::accept(ASTVisitor& v) { v.visit(*this); }
void RepeatStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ForNumStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ForInStmt::accept(ASTVisitor& v) { v.visit(*this); }
void FunctionDecl::accept(ASTVisitor& v) { v.visit(*this); }
void ReturnStmt::accept(ASTVisitor& v) { v.visit(*this); }
void BreakStmt::accept(ASTVisitor& v) { v.visit(*this); }
void LabelStmt::accept(ASTVisitor& v) { v.visit(*this); }
void GotoStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ExprStmt::accept(ASTVisitor& v) { v.visit(*this); }
