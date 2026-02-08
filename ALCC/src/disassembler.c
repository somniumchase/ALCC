#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dlfcn.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lopnames.h"
#include "alcc_utils.h"
#include "../plugin/alcc_plugin.h"

static AlccPlugin* current_plugin = NULL;

static void print_proto(Proto* p, int level);

static void print_code(Proto* p, int level) {
    char buffer[4096];

    for (int i = 0; i < p->sizecode; i++) {
        Instruction inst = p->code[i];
        OpCode op = GET_OPCODE(inst);

        printf("%*s[%03d] ", level*2, "", i+1);

        // Plugin Hook
        if (current_plugin && current_plugin->on_instruction) {
            if (current_plugin->on_instruction(p, i, buffer, sizeof(buffer))) {
                printf("%s\n", buffer);
                continue;
            }
        }

        printf("%-12s", opnames[op]);

        int a = GETARG_A(inst);

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
                    alcc_print_string(getstr(tsvalue(k)), tsslen(tsvalue(k)));
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
        if (u->name) alcc_print_string(getstr(u->name), tsslen(u->name));
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
            alcc_print_string(getstr(tsvalue(k)), tsslen(tsvalue(k)));
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

static void load_plugin(const char* path) {
    void* handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "Error loading plugin %s: %s\n", path, dlerror());
        exit(1);
    }

    alcc_plugin_init_fn init = (alcc_plugin_init_fn)dlsym(handle, "alcc_plugin_init");
    if (!init) {
        fprintf(stderr, "Plugin %s does not export alcc_plugin_init\n", path);
        exit(1);
    }

    current_plugin = init();
    printf("; Loaded plugin: %s\n", current_plugin->name);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.luac [-p plugin.so]\n", argv[0]);
        return 1;
    }

    const char* input_file = NULL;

    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            if (i+1 < argc) {
                load_plugin(argv[i+1]);
                i++;
            } else {
                fprintf(stderr, "Missing plugin path\n");
                return 1;
            }
        } else {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "Input file required\n");
        return 1;
    }

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        return 1;
    }

    StkId o = L->top.p - 1;
    if (!ttisLclosure(s2v(o))) {
        fprintf(stderr, "Not a Lua closure\n");
        return 1;
    }

    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    if (current_plugin && current_plugin->post_load) {
        current_plugin->post_load(L, p);
    }

    print_proto(p, 0);

    lua_close(L);
    return 0;
}
