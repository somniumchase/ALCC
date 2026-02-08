#ifndef ALCC_AST_H
#define ALCC_AST_H

#include <string>
#include <vector>
#include <iostream>

// Forward declarations
class ASTVisitor;

// Base Node
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& v) = 0;
};

// Statements
class Statement : public ASTNode {
public:
    virtual ~Statement() = default;
};

class Expression : public ASTNode {
public:
    virtual ~Expression() = default;
};

// Block (Scope)
class Block : public Statement {
public:
    std::vector<Statement*> statements;

    ~Block() {
        for (auto s : statements) delete s;
    }

    void add(Statement* stmt) {
        statements.push_back(stmt);
    }

    void accept(ASTVisitor& v) override;
};

// Expressions

class Literal : public Expression {
public:
    enum Type { NIL, BOOLEAN, NUMBER, STRING };
    Type type;
    std::string string_val;
    double number_val;
    bool bool_val;

    Literal() : type(NIL) {}
    Literal(bool b) : type(BOOLEAN), bool_val(b) {}
    Literal(double n) : type(NUMBER), number_val(n) {}
    Literal(const std::string& s) : type(STRING), string_val(s) {}

    void accept(ASTVisitor& v) override;
};

class Variable : public Expression {
public:
    std::string name;
    bool is_upvalue;

    Variable(const std::string& n, bool up = false) : name(n), is_upvalue(up) {}
    void accept(ASTVisitor& v) override;
};

class BinaryExpr : public Expression {
public:
    Expression* left;
    std::string op;
    Expression* right;

    BinaryExpr(Expression* l, const std::string& o, Expression* r)
        : left(l), op(o), right(r) {}

    ~BinaryExpr() { delete left; delete right; }
    void accept(ASTVisitor& v) override;
};

class UnaryExpr : public Expression {
public:
    std::string op;
    Expression* expr;

    UnaryExpr(const std::string& o, Expression* e) : op(o), expr(e) {}
    ~UnaryExpr() { delete expr; }
    void accept(ASTVisitor& v) override;
};

class FunctionCall : public Expression {
public:
    Expression* func;
    std::vector<Expression*> args;
    bool is_method_call; // obj:method()
    std::string method_name;

    FunctionCall(Expression* f) : func(f), is_method_call(false) {}
    ~FunctionCall() {
        delete func;
        for(auto a : args) delete a;
    }
    void accept(ASTVisitor& v) override;
};

class TableConstructor : public Expression {
public:
    struct Field {
        Expression* key; // nullptr for list part
        Expression* value;
    };
    std::vector<Field> fields;

    ~TableConstructor() {
        for(auto& f : fields) {
            if(f.key) delete f.key;
            delete f.value;
        }
    }
    void accept(ASTVisitor& v) override;
};

class ClosureExpr : public Expression {
public:
    std::vector<std::string> params;
    bool is_vararg;
    Block* body;

    ClosureExpr(Block* b) : is_vararg(false), body(b) {}
    ~ClosureExpr() { delete body; }
    void accept(ASTVisitor& v) override;
};

// Statements

class Assignment : public Statement {
public:
    std::vector<Expression*> targets;
    std::vector<Expression*> values;
    bool is_local;

    Assignment(bool local = false) : is_local(local) {}
    ~Assignment() {
        for(auto t : targets) delete t;
        for(auto v : values) delete v;
    }
    void accept(ASTVisitor& v) override;
};

class IfStmt : public Statement {
public:
    struct Clause {
        Expression* condition; // nullptr for else
        Block* block;
    };
    std::vector<Clause> clauses;

    ~IfStmt() {
        for(auto& c : clauses) {
            if(c.condition) delete c.condition;
            delete c.block;
        }
    }
    void accept(ASTVisitor& v) override;
};

class WhileStmt : public Statement {
public:
    Expression* condition;
    Block* body;

    WhileStmt(Expression* c, Block* b) : condition(c), body(b) {}
    ~WhileStmt() { delete condition; delete body; }
    void accept(ASTVisitor& v) override;
};

class RepeatStmt : public Statement {
public:
    Block* body;
    Expression* condition;

    RepeatStmt(Block* b, Expression* c) : body(b), condition(c) {}
    ~RepeatStmt() { delete body; delete condition; }
    void accept(ASTVisitor& v) override;
};

class ForNumStmt : public Statement {
public:
    std::string var_name;
    Expression* start;
    Expression* end;
    Expression* step;
    Block* body;

    ForNumStmt(const std::string& v, Expression* s, Expression* e, Expression* st, Block* b)
        : var_name(v), start(s), end(e), step(st), body(b) {}
    ~ForNumStmt() { delete start; delete end; delete step; delete body; }
    void accept(ASTVisitor& v) override;
};

class ForInStmt : public Statement {
public:
    std::vector<std::string> vars;
    std::vector<Expression*> exprs;
    Block* body;

    ForInStmt(Block* b) : body(b) {}
    ~ForInStmt() {
        for(auto e : exprs) delete e;
        delete body;
    }
    void accept(ASTVisitor& v) override;
};

class FunctionDecl : public Statement {
public:
    std::string name; // empty for anonymous/local func
    std::vector<std::string> params;
    bool is_vararg;
    Block* body;
    bool is_local;

    FunctionDecl(const std::string& n, Block* b, bool local=false)
        : name(n), is_vararg(false), body(b), is_local(local) {}
    ~FunctionDecl() { delete body; }
    void accept(ASTVisitor& v) override;
};

class ReturnStmt : public Statement {
public:
    std::vector<Expression*> values;

    ~ReturnStmt() { for(auto v : values) delete v; }
    void accept(ASTVisitor& v) override;
};

class BreakStmt : public Statement {
public:
    void accept(ASTVisitor& v) override;
};

class LabelStmt : public Statement {
public:
    std::string label;
    LabelStmt(const std::string& l) : label(l) {}
    void accept(ASTVisitor& v) override;
};

class GotoStmt : public Statement {
public:
    std::string label;
    GotoStmt(const std::string& l) : label(l) {}
    void accept(ASTVisitor& v) override;
};

class ExprStmt : public Statement {
public:
    Expression* expr;
    ExprStmt(Expression* e) : expr(e) {}
    ~ExprStmt() { delete expr; }
    void accept(ASTVisitor& v) override;
};

// Visitor Interface
class ASTVisitor {
public:
    virtual void visit(Block& node) = 0;
    virtual void visit(Literal& node) = 0;
    virtual void visit(Variable& node) = 0;
    virtual void visit(BinaryExpr& node) = 0;
    virtual void visit(UnaryExpr& node) = 0;
    virtual void visit(FunctionCall& node) = 0;
    virtual void visit(TableConstructor& node) = 0;
    virtual void visit(ClosureExpr& node) = 0;
    virtual void visit(Assignment& node) = 0;
    virtual void visit(IfStmt& node) = 0;
    virtual void visit(WhileStmt& node) = 0;
    virtual void visit(RepeatStmt& node) = 0;
    virtual void visit(ForNumStmt& node) = 0;
    virtual void visit(ForInStmt& node) = 0;
    virtual void visit(FunctionDecl& node) = 0;
    virtual void visit(ReturnStmt& node) = 0;
    virtual void visit(BreakStmt& node) = 0;
    virtual void visit(LabelStmt& node) = 0;
    virtual void visit(GotoStmt& node) = 0;
    virtual void visit(ExprStmt& node) = 0;
};

#endif
