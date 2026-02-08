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
#include "lopnames.h"
#include "lstring.h"
#include "alcc_utils.h"

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
    // Print function header
    printf("%*sfunction func_%p(", level*2, "", p);
    // params
    for (int i=0; i<p->numparams; i++) {
        if (i>0) printf(", ");
        printf("P%d", i);
    }
    if (isvararg(p)) {
        if (p->numparams > 0) printf(", ");
        printf("...");
    }
    printf(")\n");

    // Code
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
        (void)sj; (void)ax; (void)sbx; (void)k; // suppress unused warning for now

        printf("%*s  ", level*2, "");

        // Switch opcode
        switch(op) {
            case OP_MOVE:
                printf("local R[%d] = R[%d]", a, b);
                break;
            case OP_LOADI:
                printf("local R[%d] = %d", a, sbx);
                break;
            case OP_LOADF:
                printf("local R[%d] = %d", a, sbx); // float cast?
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
                else if (c==1) {} // No return
                else if (c==2) printf("R[%d] = ", a); // 1 return
                else printf("R[%d]..R[%d] = ", a, a+c-2);

                printf("R[%d](", a);
                if (b==0) printf("..."); // Mult args
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
                 if (b==0) printf("..."); // Mult return
                 else {
                     for (int j=0; j<b-1; j++) {
                         if (j>0) printf(", ");
                         printf("R[%d]", a+j);
                     }
                 }
                break;

            case OP_RETURN0:
                printf("return");
                break;
            case OP_RETURN1:
                printf("return R[%d]", a);
                break;

            case OP_VARARGPREP:
                printf("-- VARARGPREP");
                break;

            default:
                printf("; %s %d %d %d", opnames[op], a, b, c);
        }
        printf("\n");
    }

    printf("%*send\n", level*2, "");

    // Inner protos
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
