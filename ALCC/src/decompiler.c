#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstring.h"
#include "alcc_utils.h"
#include "alcc_backend.h"
#include "../plugin/alcc_plugin.h"

#define MAX_LABELS 1000

static AlccPlugin* current_plugin = NULL;
static Proto* printed_proto_list[1000];
static int printed_proto_count = 0;

static int is_proto_printed(Proto* p) {
    for (int i=0; i<printed_proto_count; i++) {
        if (printed_proto_list[i] == p) return 1;
    }
    return 0;
}

static void set_proto_printed(Proto* p) {
    if (printed_proto_count < 1000) {
        printed_proto_list[printed_proto_count++] = p;
    }
}

typedef struct {
    int targets[MAX_LABELS];
    int count;
} JumpAnalysis;

// Block Stack for structured control flow
typedef struct {
    int target_pc; // PC where the block ends
    int type;      // 0: IF, 1: LOOP (handled by indent but maybe needed for break)
} Block;

typedef struct {
    Block blocks[100];
    int top;
} BlockStack;

static void bs_push(BlockStack* bs, int target, int type) {
    if (bs->top < 100) {
        bs->blocks[bs->top].target_pc = target;
        bs->blocks[bs->top].type = type;
        bs->top++;
    }
}

static int bs_check_end(BlockStack* bs, int pc) {
    if (bs->top > 0 && bs->blocks[bs->top-1].target_pc == pc) {
        bs->top--;
        return 1;
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

        if (op == OP_JMP) {
            target = i + 1 + dec.bx;
        } else if (op == OP_FORLOOP || op == OP_TFORLOOP) {
            target = i + 1 - dec.bx;
        } else if (op == OP_FORPREP) {
            target = i + 1 + dec.bx + 1;
        }

        if (target >= 0 && target < p->sizecode) {
            int found = 0;
            for (int j=0; j<ja->count; j++) {
                if (ja->targets[j] == target) { found=1; break; }
            }
            if (!found && ja->count < MAX_LABELS) {
                ja->targets[ja->count++] = target;
            }
        }
    }

    // Sort
    for (int i=0; i<ja->count-1; i++) {
        for (int j=i+1; j<ja->count; j++) {
            if (ja->targets[i] > ja->targets[j]) {
                int tmp = ja->targets[i];
                ja->targets[i] = ja->targets[j];
                ja->targets[j] = tmp;
            }
        }
    }
}

static int get_label_id(JumpAnalysis* ja, int pc) {
    for (int i=0; i<ja->count; i++) {
        if (ja->targets[i] == pc) return i;
    }
    return -1;
}

static const char* resolve_var_name(Proto* p, int reg, int pc) {
    return luaF_getlocalname(p, reg + 1, pc);
}

static void print_var(Proto* p, int reg, int pc) {
    const char* name = resolve_var_name(p, reg, pc);
    if (name) printf("%s", name);
    else printf("R[%d]", reg);
}

static void decompile(Proto* p, int level);

static void print_const(Proto* p, int k) {
    TValue* val = &p->k[k];
    if (ttisstring(val)) {
        alcc_print_string(getstr(tsvalue(val)), tsslen(tsvalue(val)));
    }
    else if (ttisinteger(val)) printf("%lld", ivalue(val));
    else if (ttisnumber(val)) printf("%f", fltvalue(val));
    else if (ttisnil(val)) printf("nil");
    else if (ttisboolean(val)) printf(ttistrue(val) ? "true" : "false");
    else printf("?");
}

static void decompile(Proto* p, int level) {
    JumpAnalysis ja;
    analyze_jumps(p, &ja);

    BlockStack bs;
    bs.top = 0;

    // Indentation level for body
    int indent = level + 1;

    printf("%*sfunction func_%p(", level*2, "", p);
    for (int i=0; i<p->numparams; i++) {
        if (i>0) printf(", ");
        printf("P%d", i);
    }
    if (isvararg(p)) {
        if (p->numparams > 0) printf(", ");
        printf("...");
    }
    printf(")\n");

    char buffer[4096];
    AlccInstruction dec;

    for (int i=0; i<p->sizecode; i++) {
        if (current_plugin && current_plugin->on_decompile_inst) {
             if (current_plugin->on_decompile_inst(p, i, buffer, sizeof(buffer))) {
                 printf("%*s%s\n", level*2, "", buffer);
                 continue;
             }
        }

        int lbl = get_label_id(&ja, i);
        if (lbl >= 0) {
            printf("%*s::L%d::\n", (indent-1)*2, "", lbl);
        }

        current_backend->decode_instruction((uint32_t)p->code[i], &dec);

        // Check for end of blocks (IF/ELSE)
        while (bs_check_end(&bs, i)) {
            indent--;
            printf("%*send\n", indent*2, "");
        }

        // Adjust indent for end of loops
        if (dec.op == OP_FORLOOP || dec.op == OP_TFORLOOP) indent--;

        printf("%*s  ", indent*2, "");

        int op = dec.op;
        int a = dec.a;
        int b = dec.b;
        int c = dec.c;
        int k = dec.k;
        int bx = dec.bx;

        switch(op) {
            case OP_MOVE:
                print_var(p, a, i); printf(" = "); print_var(p, b, i);
                break;
            case OP_LOADI:
            case OP_LOADF:
                print_var(p, a, i); printf(" = %d", bx);
                break;
            case OP_LOADK:
                print_var(p, a, i); printf(" = "); print_const(p, bx);
                break;
            case OP_CLOSURE: {
                Proto* sub = p->p[bx];
                print_var(p, a, i); printf(" = ");
                // Inline decompile
                // We need to suppress the header "function func_..." and "end" and handle indent.
                // But decompile() prints header.
                // Let's refactor decompile to accept a flag?
                // Or just print "function" here and call a body_decompile function.
                // For now, let's just call decompile recursively and fix formatting later or accept standard output.
                // Better:
                decompile(sub, level + 1);
                set_proto_printed(sub);
                break;
            }
            case OP_GETUPVAL:
                print_var(p, a, i); printf(" = U[%d]", b); // TODO: Resolve upvalue name from p->upvalues[b].name
                break;
            case OP_SETUPVAL:
                printf("U[%d] = ", b); print_var(p, a, i);
                break;
            case OP_GETTABUP:
                print_var(p, a, i); printf(" = U[%d][", b); print_const(p, c); printf("]");
                break;
            case OP_GETTABLE:
                print_var(p, a, i); printf(" = "); print_var(p, b, i); printf("["); print_var(p, c, i); printf("]");
                break;
            case OP_SETTABUP:
                printf("U[%d][", a); print_const(p, b); printf("] = ");
                // Need to know if C is register or constant/immediate.
                // In Lua 5.4+, some SETTABUP take RK or specialized.
                // Assuming R for now based on backend decode which puts generic args.
                // But wait, in 5.4, SETTABUP A B C: UpValue[A][K[B]] = RK(C)
                // If backend maps C correctly...
                printf("R[%d]", c);
                break;
            case OP_CALL:
                if (c==0) printf("multret = ");
                else if (c==1) {}
                else if (c==2) { print_var(p, a, i); printf(" = "); }
                else printf("R[%d].. = ", a);

                print_var(p, a, i); printf("(");
                if (b==0) printf("...");
                else {
                    for (int j=1; j<b; j++) {
                        if (j>1) printf(", ");
                        print_var(p, a+j, i);
                    }
                }
                printf(")");
                break;
            case OP_RETURN:
                printf("return ");
                 if (b==0) printf("...");
                 else {
                     for (int j=0; j<b-1; j++) {
                         if (j>0) printf(", ");
                         print_var(p, a+j, i);
                     }
                 }
                break;
            case OP_RETURN0: printf("return"); break;
            case OP_RETURN1: printf("return "); print_var(p, a, i); break;
            case OP_JMP: {
                int dest = i + 1 + bx;
                int dest_lbl = get_label_id(&ja, dest);
                if (dest_lbl >= 0) printf("goto L%d", dest_lbl);
                else printf("goto %d", dest);
                break;
            }
            case OP_FORPREP: {
                int dest = i + 1 + bx + 1;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("for "); print_var(p, a+3, i); printf(" = ... do -- jump to L%d", dest_lbl);
                 indent++;
                 break;
            }
            case OP_FORLOOP: {
                 int dest = i + 1 - bx;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("end -- forloop jump to L%d", dest_lbl);
                 break;
            }
            case OP_TFORPREP: {
                 int dest = i + 1 + bx;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("for <gen> in ... do -- jump to L%d", dest_lbl);
                 indent++;
                 break;
            }
            case OP_TFORLOOP: {
                 int dest = i + 1 - bx;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("end -- tforloop jump to L%d", dest_lbl);
                 break;
            }
            // Arithmetic
            case OP_ADD: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" + "); print_var(p, c, i); break;
            case OP_SUB: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" - "); print_var(p, c, i); break;
            case OP_MUL: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" * "); print_var(p, c, i); break;
            case OP_DIV: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" / "); print_var(p, c, i); break;
            case OP_IDIV: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" // "); print_var(p, c, i); break;
            case OP_MOD: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" %% "); print_var(p, c, i); break;
            case OP_POW: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" ^ "); print_var(p, c, i); break;

            // Bitwise
            case OP_BAND: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" & "); print_var(p, c, i); break;
            case OP_BOR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" | "); print_var(p, c, i); break;
            case OP_BXOR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" ~ "); print_var(p, c, i); break;
            case OP_SHL: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" << "); print_var(p, c, i); break;
            case OP_SHR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" >> "); print_var(p, c, i); break;

            // Unary
            case OP_UNM: print_var(p, a, i); printf(" = -"); print_var(p, b, i); break;
            case OP_BNOT: print_var(p, a, i); printf(" = ~"); print_var(p, b, i); break;
            case OP_NOT: print_var(p, a, i); printf(" = not "); print_var(p, b, i); break;
            case OP_LEN: print_var(p, a, i); printf(" = #"); print_var(p, b, i); break;

            // Comparison -> If
            case OP_EQ:
            case OP_LT:
            case OP_LE: {
                // Peek next instruction for JMP
                if (i + 1 < p->sizecode) {
                    AlccInstruction next_dec;
                    current_backend->decode_instruction((uint32_t)p->code[i+1], &next_dec);
                    if (next_dec.op == OP_JMP) {
                        int dest = i + 1 + 1 + next_dec.bx; // pc+1 (next) + 1 + offset
                        // Logic inverted: if condition met, we skip jump. If not met, we jump (to else/end).
                        // So if we are here (not jumping), condition is TRUE (if k=1) or FALSE (if k=0)?
                        // Lua 5.4: if ((R[A] == R[B]) ~= k) then pc++
                        // If result is true (match k), we skip JMP.
                        // So the block immediately following is the "THEN" block.
                        // The JMP target is the "ELSE/END" block.

                        const char* op_str = (op==OP_EQ) ? "==" : (op==OP_LT) ? "<" : "<=";
                        printf("if ("); print_var(p, a, i); printf(" %s ", op_str); print_var(p, b, i);
                        printf(") %s then", k ? "" : "not"); // Simplified logic

                        indent++;
                        bs_push(&bs, dest, 0);
                        // Skip the JMP in output?
                        // Actually we print instructions linearly. The JMP is next.
                        // We should suppress printing the JMP if it's part of structure.
                        // For now, let's just print structure start.
                        break;
                    }
                }
                // Fallback
                const char* op_str = (op==OP_EQ) ? "==" : (op==OP_LT) ? "<" : "<=";
                printf("if ("); print_var(p, a, i); printf(" %s ", op_str); print_var(p, b, i); printf(") ~= %d then", k);
                break;
            }

            // Immediate
            case OP_ADDI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" + %d", c); break; // c is sC
            case OP_SHLI: print_var(p, a, i); printf(" = %d << ", c); print_var(p, b, i); break; // sC << R[B]
            case OP_SHRI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" >> %d", c); break;

            // Suppress MMBIN
            case OP_MMBIN:
            case OP_MMBINI:
            case OP_MMBINK:
                printf("-- mmbin");
                break;

            default: {
                const char* name = current_backend->get_op_name(op);
                if (name) printf("; %s", name);
                else printf("; OP_%d", op);
            }
        }
        printf("\n");
    }

    printf("%*send\n", level*2, "");

    for (int i=0; i<p->sizep; i++) {
        if (!is_proto_printed(p->p[i])) {
            decompile(p->p[i], level+1);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.luac\n", argv[0]);
        return 1;
    }

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, argv[1]) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        return 1;
    }

    StkId o = L->top.p - 1;
    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    decompile(p, 0);

    lua_close(L);
    return 0;
}
