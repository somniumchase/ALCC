#include "DecompilerCore.h"
#include "../ast/ASTPrinter.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstring.h"
}

#include "../core/alcc_utils.h"
#include "../core/alcc_backend.h"

#define MAX_LABELS 1000
#define BLOCK_IF 0
#define BLOCK_LOOP 1
#define BLOCK_WHILE 2
#define BLOCK_REPEAT 3

#define TARGET_NORMAL 0
#define TARGET_REPEAT 1

static int is_identifier(const char* s) {
    size_t len = strlen(s);
    if (len == 0) return 0;
    if (!isalpha((unsigned char)s[0]) && s[0] != '_') return 0;
    for (size_t i = 1; i < len; i++) {
        if (!isalnum((unsigned char)s[i]) && s[i] != '_') return 0;
    }
    static const char* keywords[] = {
        "and", "break", "do", "else", "elseif",
        "end", "false", "for", "function", "goto", "if",
        "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while",
        NULL
    };
    for (int i = 0; keywords[i]; i++) {
        if (strcmp(s, keywords[i]) == 0) return 0;
    }
    return 1;
}

struct JumpAnalysis {
    int targets[MAX_LABELS];
    int types[MAX_LABELS];
    int count;
};

struct AnalysisBlock {
    int target_pc;
    int start_pc;
    int type;
    Statement* ast_stmt; // Pointer to IfStmt, WhileStmt, etc.
    Block* ast_block;    // The block we are currently filling
};

struct BlockStack {
    AnalysisBlock blocks[100];
    int top;
};

static void bs_push(BlockStack* bs, int target, int type, Statement* stmt, Block* blk, int start_pc = -1) {
    if (bs->top < 100) {
        bs->blocks[bs->top].target_pc = target;
        bs->blocks[bs->top].start_pc = start_pc;
        bs->blocks[bs->top].type = type;
        bs->blocks[bs->top].ast_stmt = stmt;
        bs->blocks[bs->top].ast_block = blk;
        bs->top++;
    }
}

// Returns: 0 = nothing, 1 = pop (end), 2 = else transition
static int bs_check_end(BlockStack* bs, int pc, Proto* p) {
    if (bs->top > 0) {
        if (bs->blocks[bs->top-1].type == BLOCK_REPEAT) return 0;

        if (bs->blocks[bs->top-1].target_pc <= pc) {
            if (pc > 0) {
                 Instruction prev = p->code[pc-1];
                 if (GET_OPCODE(prev) == OP_JMP) {
                     int sJ = GETARG_sJ(prev);
                     int target = pc + sJ;
                     if (bs->blocks[bs->top-1].type == BLOCK_IF && target > pc) {
                         bs->blocks[bs->top-1].target_pc = target;
                         return 2;
                     }
                 }
            }
            bs->top--;
            return 1;
        }
    }
    return 0;
}

static int is_toclose(Proto* p, int pc, int reg) {
    if (pc + 1 < p->sizecode) {
        AlccInstruction next;
        current_backend->decode_instruction((uint32_t)p->code[pc+1], &next);
        if (next.op == OP_TBC && next.a == reg) return 1;
    }
    return 0;
}

static void analyze_jumps(Proto* p, JumpAnalysis* ja) {
    ja->count = 0;
    AlccInstruction dec;
    for (int i=0; i<p->sizecode; i++) {
        current_backend->decode_instruction((uint32_t)p->code[i], &dec);
        int op = dec.op;
        int target = -1;
        int type = TARGET_NORMAL;
        if (op == OP_JMP) {
            target = i + 1 + dec.bx;
        } else if (op == OP_FORLOOP || op == OP_TFORLOOP) {
            target = i + 1 - dec.bx;
        } else if (op == OP_FORPREP) {
            target = i + 1 + dec.bx + 1;
        } else if (op == OP_EQ || op == OP_LT || op == OP_LE || op == OP_EQK || op == OP_EQI ||
                   op == OP_LTI || op == OP_LEI || op == OP_GTI || op == OP_GEI ||
                   op == OP_TEST || op == OP_TESTSET) {
             if (i + 1 < p->sizecode) {
                 AlccInstruction next;
                 current_backend->decode_instruction((uint32_t)p->code[i+1], &next);
                 if (next.op == OP_JMP) {
                     int dest = i + 1 + 1 + next.bx;
                     if (dest <= i) {
                         target = dest;
                         type = TARGET_REPEAT;
                     }
                 }
             }
        }
        if (target >= 0 && target < p->sizecode) {
            int found = -1;
            for (int j=0; j<ja->count; j++) {
                if (ja->targets[j] == target) { found=j; break; }
            }
            if (found >= 0) {
                 if (type == TARGET_REPEAT) ja->types[found] = TARGET_REPEAT;
            } else if (ja->count < MAX_LABELS) {
                ja->targets[ja->count] = target;
                ja->types[ja->count] = type;
                ja->count++;
            }
        }
    }
    for (int i=0; i<ja->count-1; i++) {
        for (int j=i+1; j<ja->count; j++) {
            if (ja->targets[i] > ja->targets[j]) {
                int tmp = ja->targets[i]; ja->targets[i] = ja->targets[j]; ja->targets[j] = tmp;
                tmp = ja->types[i]; ja->types[i] = ja->types[j]; ja->types[j] = tmp;
            }
        }
    }
}

static int get_label_id(JumpAnalysis* ja, int pc) {
    for (int i=0; i<ja->count; i++) if (ja->targets[i] == pc) return i;
    return -1;
}
static int get_label_type(JumpAnalysis* ja, int pc) {
    for (int i=0; i<ja->count; i++) if (ja->targets[i] == pc) return ja->types[i];
    return TARGET_NORMAL;
}

struct DecompilerContext {
    Proto* p;
    BlockStack bs;
    JumpAnalysis ja;
    Block* current_block;
    Block* root_block;
    std::vector<Expression*> pending_regs;

    DecompilerContext(Proto* proto) : p(proto), current_block(nullptr), root_block(nullptr) {
        bs.top = 0;
        pending_regs.resize(p->maxstacksize, nullptr);
    }

    bool is_safe_to_inline(Expression* expr) {
        if (!expr) return false;
        if (dynamic_cast<Literal*>(expr)) return true;
        if (dynamic_cast<Variable*>(expr)) return true;
        if (auto* ue = dynamic_cast<UnaryExpr*>(expr)) return is_safe_to_inline(ue->expr);
        if (auto* be = dynamic_cast<BinaryExpr*>(expr)) return is_safe_to_inline(be->left) && is_safe_to_inline(be->right);
        return false;
    }

    Expression* clone_expr(Expression* expr) {
        if (!expr) return nullptr;
        if (auto* l = dynamic_cast<Literal*>(expr)) {
             if (l->type == Literal::STRING) return new Literal(l->string_val);
             if (l->type == Literal::NUMBER) return new Literal(l->number_val);
             if (l->type == Literal::BOOLEAN) return new Literal(l->bool_val);
             return new Literal();
        }
        if (auto* v = dynamic_cast<Variable*>(expr)) {
             return new Variable(v->name, v->is_upvalue);
        }
        if (auto* ue = dynamic_cast<UnaryExpr*>(expr)) {
             return new UnaryExpr(ue->op, clone_expr(ue->expr));
        }
        if (auto* be = dynamic_cast<BinaryExpr*>(expr)) {
             return new BinaryExpr(clone_expr(be->left), be->op, clone_expr(be->right));
        }
        return nullptr;
    }

    Expression* make_var(int reg, int pc) {
        const char* name = luaF_getlocalname(p, reg + 1, pc);
        if (name) return new Variable(name);
        if (reg < p->numparams) return new Variable("P" + std::to_string(reg));
        return new Variable("v" + std::to_string(reg));
    }

    Expression* make_upval(int idx) {
        if (idx < p->sizeupvalues && p->upvalues[idx].name) {
            const char* name = getstr(p->upvalues[idx].name);
            if (is_identifier(name)) return new Variable(name, true);
            return new Literal(std::string(name));
        }
        return new Variable("upval_" + std::to_string(idx), true);
    }

    Expression* make_const(int k) {
        TValue* val = &p->k[k];
        if (ttisstring(val)) return new Literal(std::string(getstr(tsvalue(val))));
        if (ttisinteger(val)) return new Literal((double)ivalue(val));
        if (ttisnumber(val)) return new Literal(fltvalue(val));
        if (ttisnil(val)) return new Literal();
        if (ttisboolean(val)) return new Literal(ttistrue(val));
        return new Literal("?");
    }

    Expression* get_expr(int reg, int pc) {
        if ((size_t)reg < pending_regs.size() && pending_regs[reg] != nullptr) {
            if (is_safe_to_inline(pending_regs[reg])) {
                // Clone instead of consume
                return clone_expr(pending_regs[reg]);
            }
            else {
                // Should not happen if I only put safe things in pending.
                // But if it is there, flush it?
                flush_pending(reg, pc);
                return make_var(reg, pc);
            }
        }
        return make_var(reg, pc);
    }

    void set_expr(int reg, Expression* expr) {
        if ((size_t)reg < pending_regs.size()) {
            if (pending_regs[reg]) {
                // Overwriting unconsumed expression. Flush it first?
                // Or delete it? If it was pure side-effect-free, we can delete.
                // But it might be an important calculation.
                // E.g. local a = 1+2; a = 3;
                // 1+2 is lost.
                // In Lua, side-effect free exprs can be discarded.
                // But to be safe, maybe flush?
                // Actually, if I overwrite, it means the value is dead.
                delete pending_regs[reg];
            }
            pending_regs[reg] = expr;
        }
    }

    void flush_pending(int reg, int pc) {
        if ((size_t)reg < pending_regs.size() && pending_regs[reg]) {
            Assignment* assign = new Assignment(false);
            assign->targets.push_back(make_var(reg, pc));
            assign->values.push_back(pending_regs[reg]);
            current_block->add(assign);
            pending_regs[reg] = nullptr;
        }
    }

    void flush_all_pending(int pc) {
        for (size_t i = 0; i < pending_regs.size(); i++) {
            flush_pending(i, pc);
        }
    }
};

static Proto* printed_proto_list[1000];
static int printed_proto_count = 0;
static int is_proto_printed(Proto* p) {
    for(int i=0;i<printed_proto_count;i++) if(printed_proto_list[i]==p) return 1;
    return 0;
}
static void set_proto_printed(Proto* p) {
    if(printed_proto_count<1000) printed_proto_list[printed_proto_count++]=p;
}

// Helpers
static void process_arithmetic(DecompilerContext& ctx, int pc, int op, int a, int b, int c, bool is_k) {
    Expression* left = ctx.get_expr(b, pc);
    Expression* right = is_k ? ctx.make_const(c) : ctx.get_expr(c, pc);
    std::string op_str = "?";
    switch(op) {
        case OP_ADD: op_str = "+"; break;
        case OP_SUB: op_str = "-"; break;
        case OP_MUL: op_str = "*"; break;
        case OP_DIV: op_str = "/"; break;
        case OP_MOD: op_str = "%"; break;
        case OP_POW: op_str = "^"; break;
        case OP_IDIV: op_str = "//"; break;
        case OP_BAND: op_str = "&"; break;
        case OP_BOR: op_str = "|"; break;
        case OP_BXOR: op_str = "~"; break;
        case OP_SHL: op_str = "<<"; break;
        case OP_SHR: op_str = ">>"; break;
    }
    Expression* bin = new BinaryExpr(left, op_str, right);
    if (ctx.is_safe_to_inline(bin)) {
        ctx.set_expr(a, bin);
    } else {
        // If not safe (shouldn't happen for binary of safe ops), assign.
        // But BinaryExpr ownership check: if left/right are pending, they are now owned by bin.
        // If bin is put in pending, good.
        // If bin is assigned, good.
        ctx.set_expr(a, bin);
    }
}

static void process_unary(DecompilerContext& ctx, int pc, int op, int a, int b) {
    Expression* val = ctx.get_expr(b, pc);
    std::string op_str = "?";
    switch(op) {
        case OP_UNM: op_str = "-"; break;
        case OP_BNOT: op_str = "~"; break;
        case OP_NOT: op_str = "not"; break;
        case OP_LEN: op_str = "#"; break;
    }
    Expression* un = new UnaryExpr(op_str, val);
    ctx.set_expr(a, un);
}

// Helper to check conditional jump
static int is_conditional_jump(Proto* p, int pc, int* target) {
    if (pc >= p->sizecode) return 0;
    AlccInstruction dec;
    current_backend->decode_instruction((uint32_t)p->code[pc], &dec);
    int op = dec.op;
    if (op == OP_EQ || op == OP_LT || op == OP_LE || op == OP_EQK || op == OP_EQI ||
        op == OP_LTI || op == OP_LEI || op == OP_GTI || op == OP_GEI ||
        op == OP_TEST || op == OP_TESTSET) {
        if (pc + 1 < p->sizecode) {
             AlccInstruction next;
             current_backend->decode_instruction((uint32_t)p->code[pc+1], &next);
             if (next.op == OP_JMP) {
                 if (target) *target = pc + 1 + 1 + next.bx;
                 return 1;
             }
        }
    }
    return 0;
}

ASTNode* DecompilerCore::build_ast(Proto* p, AlccPlugin* plugin) {
    DecompilerContext ctx(p);
    analyze_jumps(p, &ctx.ja);

    // Create Root FunctionDecl
    Block* root_block = new Block();
    ctx.root_block = root_block;
    ctx.current_block = root_block;

    FunctionDecl* func_node = new FunctionDecl("", root_block); // Name filled by caller if needed

    // Params
    for(int i=0; i<p->numparams; i++) {
        const char* name = luaF_getlocalname(p, i + 1, 0);
        if(name) func_node->params.push_back(name);
        else func_node->params.push_back("P" + std::to_string(i));
    }
    if(isvararg(p)) func_node->is_vararg = true;

    AlccInstruction dec;
    bool pending_elseif = false;

    for (int i=0; i<p->sizecode; i++) {
        int lbl = get_label_id(&ctx.ja, i);
        int lbl_type = get_label_type(&ctx.ja, i);

        if (lbl >= 0 || lbl_type != TARGET_NORMAL) {
            ctx.flush_all_pending(i);
        }

        if (lbl_type == TARGET_REPEAT) {
             RepeatStmt* rep = new RepeatStmt(new Block(), nullptr);
             ctx.current_block->add(rep);
             bs_push(&ctx.bs, -1, BLOCK_REPEAT, rep, rep->body, i);
             ctx.current_block = rep->body;
        }

        if (lbl >= 0) {
            ctx.current_block->add(new LabelStmt("L" + std::to_string(lbl)));
        }

        current_backend->decode_instruction((uint32_t)p->code[i], &dec);

        int status;
        while ((status = bs_check_end(&ctx.bs, i, p))) {
            ctx.flush_all_pending(i); // Block end boundary
            if (status == 2) {
                // ELSE transition
                int target = -1;
                IfStmt* if_stmt = (IfStmt*)ctx.bs.blocks[ctx.bs.top-1].ast_stmt;

                if (is_conditional_jump(p, i, &target)) {
                     pending_elseif = true;
                } else {
                     // Else
                     Block* else_blk = new Block();
                     if_stmt->clauses.push_back({nullptr, else_blk});
                     ctx.current_block = else_blk;
                     ctx.bs.blocks[ctx.bs.top-1].ast_block = else_blk;
                     break;
                }
            } else {
                if (ctx.bs.top > 0) ctx.current_block = ctx.bs.blocks[ctx.bs.top-1].ast_block;
                else ctx.current_block = root_block;
            }
        }

        if (dec.op == OP_FORLOOP || dec.op == OP_TFORLOOP) {
             ctx.flush_all_pending(i);
             if (ctx.bs.top > 0 && ctx.bs.blocks[ctx.bs.top-1].type == BLOCK_LOOP) {
                 ctx.bs.top--;
                 if (ctx.bs.top > 0) ctx.current_block = ctx.bs.blocks[ctx.bs.top-1].ast_block;
                 else ctx.current_block = root_block;
             }
        }

        int op = dec.op;
        int a = dec.a;
        int b = dec.b;
        int c = dec.c;
        int k = dec.k;
        int bx = dec.bx;

        switch(op) {
            case OP_MOVE: {
                ctx.set_expr(a, ctx.get_expr(b, i));
                break;
            }
            case OP_LOADI:
            case OP_LOADF: {
                ctx.set_expr(a, new Literal((double)bx));
                break;
            }
            case OP_LOADK: {
                ctx.set_expr(a, ctx.make_const(bx));
                break;
            }
            case OP_GETUPVAL: {
                ctx.set_expr(a, ctx.make_upval(b));
                break;
            }
            case OP_SETUPVAL: {
                Assignment* assign = new Assignment(false);
                assign->targets.push_back(ctx.make_upval(b));
                assign->values.push_back(ctx.get_expr(a, i));
                ctx.current_block->add(assign);
                break;
            }
            case OP_GETTABUP: {
                Expression* key = ttisstring(&p->k[c]) ? (Expression*)new Literal(std::string(getstr(tsvalue(&p->k[c])))) : ctx.make_const(c);
                ctx.set_expr(a, new BinaryExpr(ctx.make_upval(b), "[", key));
                break;
            }
            case OP_GETTABLE: {
                ctx.set_expr(a, new BinaryExpr(ctx.get_expr(b, i), "[", ctx.get_expr(c, i)));
                break;
            }
            case OP_GETI: {
                ctx.set_expr(a, new BinaryExpr(ctx.get_expr(b, i), "[", new Literal((double)c)));
                break;
            }
            case OP_GETFIELD: {
                Expression* key = ttisstring(&p->k[c]) ? (Expression*)new Literal(std::string(getstr(tsvalue(&p->k[c])))) : ctx.make_const(c);
                ctx.set_expr(a, new BinaryExpr(ctx.get_expr(b, i), "[", key));
                break;
            }
            case OP_SETTABUP: {
                Expression* key = ttisstring(&p->k[b]) ? (Expression*)new Literal(std::string(getstr(tsvalue(&p->k[b])))) : ctx.make_const(b);
                Assignment* assign = new Assignment(false);
                assign->targets.push_back(new BinaryExpr(ctx.make_upval(a), "[", key));
                assign->values.push_back(ctx.get_expr(c, i));
                ctx.current_block->add(assign);
                break;
            }
            case OP_SETTABLE: {
                Assignment* assign = new Assignment(false);
                assign->targets.push_back(new BinaryExpr(ctx.get_expr(a, i), "[", ctx.get_expr(b, i)));
                assign->values.push_back(k ? ctx.make_const(c) : ctx.get_expr(c, i));
                ctx.current_block->add(assign);
                break;
            }
            case OP_SETI: {
                Assignment* assign = new Assignment(false);
                assign->targets.push_back(new BinaryExpr(ctx.get_expr(a, i), "[", new Literal((double)b)));
                assign->values.push_back(k ? ctx.make_const(c) : ctx.get_expr(c, i));
                ctx.current_block->add(assign);
                break;
            }
            case OP_SETFIELD: {
                Expression* key = ttisstring(&p->k[b]) ? (Expression*)new Literal(std::string(getstr(tsvalue(&p->k[b])))) : ctx.make_const(b);
                Assignment* assign = new Assignment(false);
                assign->targets.push_back(new BinaryExpr(ctx.get_expr(a, i), "[", key));
                assign->values.push_back(k ? ctx.make_const(c) : ctx.get_expr(c, i));
                ctx.current_block->add(assign);
                break;
            }
            case OP_SELF: {
                 ctx.set_expr(a+1, ctx.get_expr(b, i)); // self arg
                 Expression* key = ttisstring(&p->k[c]) ? (Expression*)new Literal(std::string(getstr(tsvalue(&p->k[c])))) : ctx.make_const(c);
                 ctx.set_expr(a, new BinaryExpr(ctx.get_expr(b, i), "[", key)); // method
                 break;
            }
            case OP_ADD: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_SUB: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_MUL: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_DIV: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_MOD: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_POW: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_IDIV: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_BAND: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_BOR: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_BXOR: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_SHL: process_arithmetic(ctx, i, op, a, b, c, false); break;
            case OP_SHR: process_arithmetic(ctx, i, op, a, b, c, false); break;

            case OP_UNM: case OP_BNOT: case OP_NOT: case OP_LEN:
                process_unary(ctx, i, op, a, b);
                break;

            case OP_CONCAT: {
                ctx.flush_pending(a, i); // safety?
                // actually if a is pending, we can use it?
                // OP_CONCAT A B means R[A] := R[A] .. ... .. R[A+B-1]
                Expression* expr = ctx.get_expr(a, i);
                for(int j=1; j<b; j++) {
                    expr = new BinaryExpr(expr, "..", ctx.get_expr(a+j, i));
                }
                ctx.set_expr(a, expr);
                break;
            }
            case OP_CALL: {
                // Flush args?
                // args are A+1 ...
                // But if they are pending safe exprs, get_expr will inline them.
                FunctionCall* call = new FunctionCall(ctx.get_expr(a, i));
                for(int j=1; j<b; j++) call->args.push_back(ctx.get_expr(a+j, i));

                if (c == 0) { // multret
                    Assignment* a_stmt = new Assignment(false);
                    a_stmt->targets.push_back(new Variable("multret"));
                    a_stmt->values.push_back(call);
                    ctx.current_block->add(a_stmt);
                } else if (c == 1) { // no results
                     ctx.current_block->add(new ExprStmt(call));
                } else {
                     if (c > 1) {
                         // Results R[A]...
                         // Unsafe to inline function call multiple times.
                         // So we must assign.
                         // BUT, if results > 1, it's a multi-assignment.
                         // local v1, v2 = f()
                         // We can't put this in pending easily because pending tracks 1 reg -> 1 expr.
                         // So we must flush.
                         Assignment* a_stmt = new Assignment(false);
                         for(int j=0; j<c-1; j++) {
                             // ctx.flush_pending(a+j, i); // ensure target regs are clear?
                             // No, assignment overwrites.
                             a_stmt->targets.push_back(ctx.make_var(a+j, i));
                         }
                         a_stmt->values.push_back(call);
                         ctx.current_block->add(a_stmt);

                         // We should clear pending for these targets?
                         for(int j=0; j<c-1; j++) ctx.pending_regs[a+j] = nullptr;
                         // But we just assigned them. They are now holding the result of call.
                         // Can we say pending[a] = Variable(a)?
                         // No need, get_expr does that by default.
                     }
                }
                break;
            }
            case OP_NEWTABLE: {
                TableConstructor* tc = new TableConstructor();
                Assignment* assign = new Assignment(false);
                assign->targets.push_back(ctx.make_var(a, i));
                assign->values.push_back(tc);
                ctx.current_block->add(assign);
                // Can't easily inline table constructor logic yet with loop below.

                int table_reg = a;
                int next_pc = i + 1;
                while (next_pc < p->sizecode) {
                    AlccInstruction next_inst;
                    current_backend->decode_instruction((uint32_t)p->code[next_pc], &next_inst);
                    if (next_inst.op == OP_EXTRAARG) { next_pc++; continue; }
                    if (next_inst.op == OP_SETFIELD && next_inst.a == table_reg) {
                         Expression* key = nullptr;
                         if (ttisstring(&p->k[next_inst.b])) key = new Literal(std::string(getstr(tsvalue(&p->k[next_inst.b]))));
                         else key = ctx.make_const(next_inst.b);

                         Expression* val = next_inst.k ? ctx.make_const(next_inst.c) : ctx.get_expr(next_inst.c, next_pc);
                         tc->fields.push_back({key, val});
                         next_pc++;
                    } else if (next_inst.op == OP_SETLIST && next_inst.a == table_reg) {
                         int num = next_inst.b;
                         if (num == 0) num = 0;
                         for (int j=1; j<=num; j++) {
                             tc->fields.push_back({nullptr, ctx.get_expr(next_inst.a + j, next_pc)});
                         }
                         next_pc++;
                    } else if (next_inst.op == OP_SETI && next_inst.a == table_reg) {
                         Expression* key = new Literal((double)next_inst.b);
                         Expression* val = next_inst.k ? ctx.make_const(next_inst.c) : ctx.get_expr(next_inst.c, next_pc);
                         tc->fields.push_back({key, val});
                         next_pc++;
                    } else {
                         break;
                    }
                }
                i = next_pc - 1;
                break;
            }
            case OP_CLOSURE: {
                 // Simplified closure handling
                Proto* sub = p->p[bx];
                ASTNode* sub_ast = build_ast(sub, plugin);
                FunctionDecl* sub_func = dynamic_cast<FunctionDecl*>(sub_ast);
                std::string func_name;
                bool is_local = false;

                // TODO: Peek logic similar to before to find name
                 int next_idx = i + 1;
                if (next_idx < p->sizecode) {
                    AlccInstruction next;
                    current_backend->decode_instruction((uint32_t)p->code[next_idx], &next);
                     if (next.op == OP_SETTABUP && next.c == a) {
                         if (next.b < p->sizek && ttisstring(&p->k[next.b]) && is_identifier(getstr(tsvalue(&p->k[next.b])))) {
                             func_name = getstr(tsvalue(&p->k[next.b]));
                             i++; // skip
                         }
                    } else if (next.op == OP_MOVE && next.b == a) {
                        const char* loc = luaF_getlocalname(p, next.a + 1, next_idx);
                        if (loc) {
                            func_name = loc;
                            is_local = true;
                            i++; // skip
                        }
                    } else if (next.op == OP_SETFIELD && next.c == a && next.k == 0) {
                        if (next.b < p->sizek && ttisstring(&p->k[next.b]) && is_identifier(getstr(tsvalue(&p->k[next.b])))) {
                             const char* field = getstr(tsvalue(&p->k[next.b]));
                             const char* base = luaF_getlocalname(p, next.a + 1, next_idx);
                             if (base) {
                                 func_name = std::string(base) + "." + field;
                                 i++; // skip
                             }
                        }
                    }
                }

                if (!func_name.empty()) {
                    sub_func->name = func_name;
                    sub_func->is_local = is_local;
                    ctx.current_block->add(sub_func);
                } else {
                    Assignment* assign = new Assignment(false);
                    assign->targets.push_back(ctx.make_var(a, i));
                    ClosureExpr* closure = new ClosureExpr(sub_func->body);
                    closure->params = sub_func->params;
                    closure->is_vararg = sub_func->is_vararg;
                    sub_func->body = nullptr;
                    delete sub_func;
                    assign->values.push_back(closure);
                    ctx.current_block->add(assign);
                }
                set_proto_printed(sub);
                break;
            }
            case OP_RETURN: {
                ctx.flush_all_pending(i);
                ReturnStmt* ret = new ReturnStmt();
                if (b > 0) {
                    for(int j=0; j<b-1; j++) ret->values.push_back(ctx.get_expr(a+j, i));
                }
                ctx.current_block->add(ret);
                break;
            }

            case OP_EQ: case OP_LT: case OP_LE: case OP_EQK: case OP_EQI:
            case OP_LTI: case OP_LEI: case OP_GTI: case OP_GEI:
            case OP_TEST: case OP_TESTSET: {
                ctx.flush_all_pending(i); // Control flow
                if (i + 1 < p->sizecode) {
                    AlccInstruction next_dec;
                    current_backend->decode_instruction((uint32_t)p->code[i+1], &next_dec);
                    if (next_dec.op == OP_JMP) {
                        int dest = i + 1 + 1 + next_dec.bx;
                        bool is_while = false;
                        if (dest > i && dest <= p->sizecode) {
                             AlccInstruction back_inst;
                             if(dest>0) {
                                 current_backend->decode_instruction((uint32_t)p->code[dest-1], &back_inst);
                                 if (back_inst.op == OP_JMP) {
                                     int back_dest = (dest-1)+1+back_inst.bx;
                                     if(back_dest==i || (lbl>=0 && back_dest==i)) is_while=true;
                                 }
                             }
                        }
                        bool is_repeat = (dest <= i);
                        Expression* cond = nullptr;
                        Expression* lhs = ctx.get_expr(a, i); // Should work even if flushed above as it returns var
                        Expression* rhs = nullptr;
                        std::string op_str = "==";
                        if (op == OP_TEST || op == OP_TESTSET) {
                            cond = lhs;
                            if (k) cond = new UnaryExpr("not", cond);
                        } else {
                            if (op == OP_EQ || op == OP_LT || op == OP_LE) rhs = ctx.get_expr(b, i);
                            else if (op == OP_EQK) rhs = ctx.make_const(b);
                            else rhs = new Literal((double)(b - OFFSET_sC));

                            if (op == OP_EQ || op == OP_EQK || op == OP_EQI) op_str = k ? "~=" : "==";
                            else if (op == OP_LT || op == OP_LTI) op_str = k ? ">=" : "<";
                            else if (op == OP_LE || op == OP_LEI) op_str = k ? ">" : "<=";
                            else if (op == OP_GTI) op_str = k ? "<=" : ">";
                            else if (op == OP_GEI) op_str = k ? "<" : ">=";
                            cond = new BinaryExpr(lhs, op_str, rhs);
                        }

                        if (is_while) {
                            Block* body = new Block();
                            WhileStmt* ws = new WhileStmt(cond, body);
                            ctx.current_block->add(ws);
                            bs_push(&ctx.bs, dest, BLOCK_WHILE, ws, body, i);
                            ctx.current_block = body;
                            i++;
                        } else if (is_repeat) {
                            if (ctx.bs.top > 0 && ctx.bs.blocks[ctx.bs.top-1].type == BLOCK_REPEAT) {
                                RepeatStmt* rs = (RepeatStmt*)ctx.bs.blocks[ctx.bs.top-1].ast_stmt;
                                rs->condition = cond;
                                ctx.bs.top--;
                                if(ctx.bs.top>0) ctx.current_block = ctx.bs.blocks[ctx.bs.top-1].ast_block;
                                else ctx.current_block = root_block;
                            }
                            i++;
                        } else {
                            Block* then_blk = new Block();
                            if (pending_elseif) {
                                IfStmt* if_stmt = (IfStmt*)ctx.bs.blocks[ctx.bs.top-1].ast_stmt;
                                if_stmt->clauses.push_back({cond, then_blk});
                                ctx.bs.blocks[ctx.bs.top-1].ast_block = then_blk;
                            } else {
                                IfStmt* if_stmt = new IfStmt();
                                if_stmt->clauses.push_back({cond, then_blk});
                                ctx.current_block->add(if_stmt);
                                bs_push(&ctx.bs, dest, BLOCK_IF, if_stmt, then_blk);
                            }
                            ctx.current_block = then_blk;
                            pending_elseif = false;
                            i++;
                        }
                        break;
                    }
                }
                break;
            }

            default:
                 // Fallback or todo
                 break;
        }

        pending_elseif = false;
    }

    // End flush
    ctx.flush_all_pending(p->sizecode);

    return func_node;
}

void DecompilerCore::decompile(Proto* p, int level, AlccPlugin* plugin, const char* name_override) {
    ASTNode* root = build_ast(p, plugin);
    if (plugin && plugin->on_ast_process) plugin->on_ast_process(root);
    LuaPrinter printer;
    printer.indent_level = level;
    root->accept(printer);
    printf("\n");
}
