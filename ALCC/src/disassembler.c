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
#include <ctype.h>

static void print_string(const char* s, size_t len) {
    printf("\"");
    for (size_t i=0; i<len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') printf("\\\"");
        else if (c == '\\') printf("\\\\");
        else if (c == '\n') printf("\\n");
        else if (c == '\r') printf("\\r");
        else if (c == '\t') printf("\\t");
        else if (isprint(c)) printf("%c", c);
        else printf("\\x%02x", c);
    }
    printf("\"");
}

static void print_proto(Proto* p, int level);

static void print_code(Proto* p, int level) {
    for (int i = 0; i < p->sizecode; i++) {
        Instruction inst = p->code[i];
        OpCode op = GET_OPCODE(inst);

        printf("%*s[%03d] %-12s", level*2, "", i+1, opnames[op]);

        int a = GETARG_A(inst);
        // We use macros that don't check opmode for printing raw values safely?
        // Actually the macros in lopcodes.h usually check opmode with assert/check_exp.
        // If we compile without NDEBUG, assertions might fail if we call GETARG_B on iAsBx.
        // But here we switch on mode.

        switch (getOpMode(op)) {
            case iABC:
                printf("%d %d %d", a, GETARG_B(inst), GETARG_C(inst));
                if (GETARG_k(inst)) printf(" (k)");
                break;
            case ivABC:
                printf("%d %d %d", a, GETARG_vB(inst), GETARG_vC(inst));
                if (GETARG_k(inst)) printf(" (k)");
                break;
            case iABx:
                printf("%d %d", a, GETARG_Bx(inst));
                break;
            case iAsBx:
                printf("%d %d", a, GETARG_sBx(inst));
                break;
            case iAx:
                printf("%d", GETARG_Ax(inst));
                break;
            case isJ:
                printf("%d", GETARG_sJ(inst));
                if (GETARG_k(inst)) printf(" (k)");
                break;
        }

        // Comments for constants
        if (op == OP_LOADK) {
            int bx = GETARG_Bx(inst);
            if (bx < p->sizek) {
                TValue* k = &p->k[bx];
                if (ttisstring(k)) {
                    printf(" ; ");
                    print_string(getstr(tsvalue(k)), tsslen(tsvalue(k)));
                }
                else if (ttisinteger(k)) printf(" ; %lld", ivalue(k));
                else if (ttisnumber(k)) printf(" ; %f", fltvalue(k));
            }
        }

        printf("\n");
    }
}

static void print_proto(Proto* p, int level) {
    printf("\n%*s; Function: %p (lines %d-%d)\n", level*2, "", p, p->linedefined, p->lastlinedefined);
    printf("%*s; NumParams: %d, IsVararg: %d, MaxStackSize: %d\n", level*2, "", p->numparams, isvararg(p), p->maxstacksize);

    printf("%*s; Upvalues (%d):\n", level*2, "", p->sizeupvalues);
    for (int i = 0; i < p->sizeupvalues; i++) {
        Upvaldesc* u = &p->upvalues[i];
        printf("%*s  [%d] ", level*2, "", i);
        if (u->name) print_string(getstr(u->name), tsslen(u->name));
        else printf("(no name)");
        printf(" %d %d %d\n", u->instack, u->idx, u->kind);
    }

    printf("%*s; Constants (%d):\n", level*2, "", p->sizek);
    for (int i = 0; i < p->sizek; i++) {
        TValue* k = &p->k[i];
        printf("%*s  [%d] ", level*2, "", i);
        if (ttisnumber(k)) {
            if (ttisinteger(k)) printf("%lld", ivalue(k));
            else printf("%f", fltvalue(k));
        } else if (ttisstring(k)) {
            print_string(getstr(tsvalue(k)), tsslen(tsvalue(k)));
        } else if (ttisnil(k)) {
            printf("nil");
        } else if (ttisboolean(k)) {
            printf(ttistrue(k) ? "true" : "false");
        } else {
            printf("type(%d)", ttype(k));
        }
        printf("\n");
    }

    printf("%*s; Code (%d):\n", level*2, "", p->sizecode);
    print_code(p, level);

    printf("%*s; Protos (%d):\n", level*2, "", p->sizep);
    for (int i = 0; i < p->sizep; i++) {
        print_proto(p->p[i], level+1);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.luac\n", argv[0]);
        return 1;
    }

    lua_State* L = luaL_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, argv[1]) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        return 1;
    }

    // Access top of stack
    StkId o = L->top.p - 1;
    if (!ttisLclosure(s2v(o))) {
        fprintf(stderr, "Not a Lua closure (maybe it is a binary chunk from different version or stripped?)\n");
        return 1;
    }

    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    print_proto(p, 0);

    lua_close(L);
    return 0;
}
