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
#include "alcc_opcodes.h"

#define MAX_LABELS 1000

typedef struct {
    int targets[MAX_LABELS];
    int count;
} JumpAnalysis;

static void analyze_jumps(Proto* p, JumpAnalysis* ja) {
    ja->count = 0;
    for (int i=0; i<p->sizecode; i++) {
        Instruction inst = p->code[i];
        OpCode op = GET_OPCODE(inst);

        // Use abstract checks if possible, or just specific opcodes for logic
        if (op == OP_JMP || op == OP_FORLOOP || op == OP_FORPREP || op == OP_TFORLOOP) {
            int target = -1;
            if (op == OP_JMP) {
                int sj = GETARG_sJ(inst);
                target = i + 1 + sj;
            } else if (op == OP_FORLOOP || op == OP_FORPREP || op == OP_TFORLOOP) {
                int bx = GETARG_Bx(inst);
                target = i + 1 - bx;
                if (op == OP_FORPREP) target = i + 1 + bx + 1;
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

    for (int i=0; i<p->sizecode; i++) {
        Instruction inst = p->code[i];
        OpCode op = GET_OPCODE(inst);
        int a = GETARG_A(inst);
        int b = GETARG_B(inst);
        int c = GETARG_C(inst);
        int bx = GETARG_Bx(inst);
        int sbx = GETARG_sBx(inst);
        int ax = GETARG_Ax(inst);
        int sj = GETARG_sJ(inst);
        int k = GETARG_k(inst);
        (void)ax; (void)k;

        int lbl = get_label_id(&ja, i);
        if (lbl >= 0) {
            printf("%*s::L%d::\n", level*2, "", lbl);
        }

        printf("%*s  ", level*2, "");

        switch(op) {
            case OP_MOVE:
                printf("local R[%d] = R[%d]", a, b);
                break;
            case OP_LOADI:
                printf("local R[%d] = %d", a, sbx);
                break;
            case OP_LOADF:
                printf("local R[%d] = %d", a, sbx);
                break;
            case OP_LOADK:
                printf("local R[%d] = ", a);
                print_const(p, bx);
                break;
            case OP_GETUPVAL:
                printf("local R[%d] = U[%d]", a, b);
                break;
            case OP_SETUPVAL:
                printf("U[%d] = R[%d]", b, a);
                break;
            case OP_GETTABUP:
                printf("local R[%d] = U[%d][", a, b);
                print_const(p, c);
                printf("]");
                break;
            case OP_GETTABLE:
                printf("local R[%d] = R[%d][R[%d]]", a, b, c);
                break;
            case OP_SETTABUP:
                printf("U[%d][", a);
                print_const(p, b);
                printf("] = ");
                printf("R[%d]", c);
                break;
            case OP_CALL:
                if (c==0) printf("multret = ");
                else if (c==1) {}
                else if (c==2) printf("R[%d] = ", a);
                else printf("R[%d]..R[%d] = ", a, a+c-2);

                printf("R[%d](", a);
                if (b==0) printf("...");
                else {
                    for (int j=1; j<b; j++) {
                        if (j>1) printf(", ");
                        printf("R[%d]", a+j);
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
                         printf("R[%d]", a+j);
                     }
                 }
                break;
            case OP_RETURN0: printf("return"); break;
            case OP_RETURN1: printf("return R[%d]", a); break;
            case OP_JMP: {
                int dest = i + 1 + sj;
                int dest_lbl = get_label_id(&ja, dest);
                if (dest_lbl >= 0) printf("goto L%d", dest_lbl);
                else printf("goto %d", dest);
                break;
            }
            case OP_FORPREP: {
                int dest = i + 1 + bx + 1;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("forprep R[%d] goto L%d", a, dest_lbl);
                 break;
            }
            case OP_FORLOOP: {
                 int dest = i + 1 - bx;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("forloop R[%d] goto L%d", a, dest_lbl);
                 break;
            }
            default: {
                const char* name = alcc_get_op_name(op);
                if (name) printf("; %s %d %d %d", name, a, b, c);
                else printf("; OP_%d %d %d %d", op, a, b, c);
            }
        }
        printf("\n");
    }

    printf("%*send\n", level*2, "");

    for (int i=0; i<p->sizep; i++) {
        decompile(p->p[i], level+1);
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
