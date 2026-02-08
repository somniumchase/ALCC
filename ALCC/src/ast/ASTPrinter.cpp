#include "ASTPrinter.h"
#include <iomanip>

void LuaPrinter::print_indent() {
    for (int i = 0; i < indent_level; ++i) out << "  ";
}

void LuaPrinter::visit(Block& node) {
    for (auto stmt : node.statements) {
        // Blocks inside statements handle their own indentation often,
        // but top-level statements in a block need indentation.
        // However, some statements (If, While) have blocks as children.
        // The block visit itself shouldn't necessarily print newlines if it's inline?
        // No, in Lua blocks are chunks.

        // Check if statement is a block itself (scope)?
        // Usually we just iterate.
        stmt->accept(*this);
        out << "\n";
    }
}

void LuaPrinter::visit(Literal& node) {
    switch (node.type) {
        case Literal::NIL: out << "nil"; break;
        case Literal::BOOLEAN: out << (node.bool_val ? "true" : "false"); break;
        case Literal::NUMBER:
            // format logic matching original
            if ((long long)node.number_val == node.number_val)
                out << (long long)node.number_val;
            else
                out << node.number_val;
            break;
        case Literal::STRING:
            out << "\"";
            for (char c : node.string_val) {
                if (c == '"') out << "\\\"";
                else if (c == '\\') out << "\\\\";
                else if (c == '\n') out << "\\n";
                else if (c == '\r') out << "\\r";
                else if (c == '\t') out << "\\t";
                else if (isprint(c)) out << c;
                else out << "\\" << std::setfill('0') << std::setw(3) << (int)(unsigned char)c;
            }
            out << "\"";
            break;
    }
}

void LuaPrinter::visit(Variable& node) {
    out << node.name;
}

void LuaPrinter::visit(BinaryExpr& node) {
    // Basic precedence handling: always wrap to be safe for now,
    // unless strict logic is implemented.
    // Original decompiler didn't wrap much.
    // Let's wrap if children are binary exprs.
    // Or just wrap everything to guarantee correctness.

    if (node.op == "[") {
        node.left->accept(*this);
        out << "[";
        node.right->accept(*this);
        out << "]";
    } else {
        out << "(";
        node.left->accept(*this);
        out << " " << node.op << " ";
        node.right->accept(*this);
        out << ")";
    }
}

void LuaPrinter::visit(UnaryExpr& node) {
    out << node.op;
    if (node.op == "not") out << " "; // spacing
    node.expr->accept(*this);
}

void LuaPrinter::visit(FunctionCall& node) {
    if (node.is_method_call) {
        // We assume func is a variable or expr that evaluates to object
        // But for obj:method(), 'func' in AST might be 'obj'.
        node.func->accept(*this);
        out << ":" << node.method_name;
    } else {
        node.func->accept(*this);
    }

    out << "(";
    for (size_t i = 0; i < node.args.size(); ++i) {
        if (i > 0) out << ", ";
        node.args[i]->accept(*this);
    }
    out << ")";
}

void LuaPrinter::visit(TableConstructor& node) {
    out << "{";
    if (!node.fields.empty()) out << " ";
    for (size_t i = 0; i < node.fields.size(); ++i) {
        if (i > 0) out << ", ";
        if (node.fields[i].key) {
            // check if key is string literal valid identifier
            bool standard_key = false;
            if (Literal* lit = dynamic_cast<Literal*>(node.fields[i].key)) {
                if (lit->type == Literal::STRING) {
                    // check simple identifier
                    bool is_id = true;
                    if (lit->string_val.empty() || isdigit(lit->string_val[0])) is_id = false;
                    else {
                        for(char c : lit->string_val) {
                            if (!isalnum(c) && c != '_') { is_id = false; break; }
                        }
                    }
                    if (is_id) {
                        out << lit->string_val << " = ";
                        standard_key = true;
                    }
                }
            }
            if (!standard_key) {
                out << "[";
                node.fields[i].key->accept(*this);
                out << "] = ";
            }
        }
        node.fields[i].value->accept(*this);
    }
    if (!node.fields.empty()) out << " ";
    out << "}";
}

void LuaPrinter::visit(ClosureExpr& node) {
    out << "function(";
    for (size_t i = 0; i < node.params.size(); ++i) {
        if (i > 0) out << ", ";
        out << node.params[i];
    }
    if (node.is_vararg) {
        if (!node.params.empty()) out << ", ";
        out << "...";
    }
    out << ")\n";
    indent_level++;
    node.body->accept(*this);
    indent_level--;
    print_indent();
    out << "end";
}

void LuaPrinter::visit(Assignment& node) {
    print_indent();
    if (node.is_local) out << "local ";
    for (size_t i = 0; i < node.targets.size(); ++i) {
        if (i > 0) out << ", ";
        node.targets[i]->accept(*this);
    }
    if (!node.values.empty()) {
        out << " = ";
        for (size_t i = 0; i < node.values.size(); ++i) {
            if (i > 0) out << ", ";
            node.values[i]->accept(*this);
        }
    }
}

void LuaPrinter::visit(IfStmt& node) {
    for (size_t i = 0; i < node.clauses.size(); ++i) {
        if (i == 0) {
            print_indent();
            out << "if ";
            node.clauses[i].condition->accept(*this);
            out << " then\n";
        } else if (node.clauses[i].condition) {
            print_indent();
            out << "elseif ";
            node.clauses[i].condition->accept(*this);
            out << " then\n";
        } else {
            print_indent();
            out << "else\n";
        }

        indent_level++;
        node.clauses[i].block->accept(*this);
        indent_level--;
    }
    print_indent();
    out << "end";
}

void LuaPrinter::visit(WhileStmt& node) {
    print_indent();
    out << "while ";
    node.condition->accept(*this);
    out << " do\n";
    indent_level++;
    node.body->accept(*this);
    indent_level--;
    print_indent();
    out << "end";
}

void LuaPrinter::visit(RepeatStmt& node) {
    print_indent();
    out << "repeat\n";
    indent_level++;
    node.body->accept(*this);
    indent_level--;
    print_indent();
    out << "until ";
    node.condition->accept(*this);
}

void LuaPrinter::visit(ForNumStmt& node) {
    print_indent();
    out << "for " << node.var_name << " = ";
    node.start->accept(*this);
    out << ", ";
    node.end->accept(*this);
    if (node.step) {
        out << ", ";
        node.step->accept(*this);
    }
    out << " do\n";
    indent_level++;
    node.body->accept(*this);
    indent_level--;
    print_indent();
    out << "end";
}

void LuaPrinter::visit(ForInStmt& node) {
    print_indent();
    out << "for ";
    for (size_t i = 0; i < node.vars.size(); ++i) {
        if (i > 0) out << ", ";
        out << node.vars[i];
    }
    out << " in ";
    for (size_t i = 0; i < node.exprs.size(); ++i) {
        if (i > 0) out << ", ";
        node.exprs[i]->accept(*this);
    }
    out << " do\n";
    indent_level++;
    node.body->accept(*this);
    indent_level--;
    print_indent();
    out << "end";
}

void LuaPrinter::visit(FunctionDecl& node) {
    // If it's a statement (named function), indent.
    // If it's an expression (anonymous), no indent, no name.
    // Wait, FunctionDecl is a Statement in AST.
    // If it's inside an assignment, we use FunctionExpr?
    // Ah, I didn't create FunctionExpr. I just used FunctionDecl as a statement.
    // But anonymous functions are Expressions.
    // I should have separate classes or a flag.
    // Current design: FunctionDecl is Statement.
    // For anonymous functions in assignments, I need an Expression.
    // Let's assume FunctionDecl handles both or I need to add FunctionExpr.

    // Check if name is empty -> likely intended as expression but used as statement?
    // Actually, 'local function f()' is statement. 'f = function()' is assignment with expression.
    // The AST has 'FunctionDecl' as Statement.
    // I need 'Closure' expression.
    // I defined 'Closure' in my thought process but didn't put it in AST.h explicitly?
    // Let's check AST.h.

    print_indent();
    if (node.is_local) out << "local ";
    out << "function ";
    if (!node.name.empty()) out << node.name;
    out << "(";
    for (size_t i = 0; i < node.params.size(); ++i) {
        if (i > 0) out << ", ";
        out << node.params[i];
    }
    if (node.is_vararg) {
        if (!node.params.empty()) out << ", ";
        out << "...";
    }
    out << ")\n";
    indent_level++;
    node.body->accept(*this);
    indent_level--;
    print_indent();
    out << "end";
}

void LuaPrinter::visit(ReturnStmt& node) {
    print_indent();
    out << "return";
    if (!node.values.empty()) out << " ";
    for (size_t i = 0; i < node.values.size(); ++i) {
        if (i > 0) out << ", ";
        node.values[i]->accept(*this);
    }
}

void LuaPrinter::visit(BreakStmt& node) {
    print_indent();
    out << "break";
}

void LuaPrinter::visit(LabelStmt& node) {
    print_indent(); // Labels usually de-indented but let's keep simple
    out << "::" << node.label << "::";
}

void LuaPrinter::visit(GotoStmt& node) {
    print_indent();
    out << "goto " << node.label;
}

void LuaPrinter::visit(ExprStmt& node) {
    print_indent();
    node.expr->accept(*this);
}
