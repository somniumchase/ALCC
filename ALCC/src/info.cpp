#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <set>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstring.h"
}
#include "alcc_utils.h"
#include "core/compat.h"
#include "alcc_backend.h"

static AlccBackend* current_backend_ptr = NULL;
static std::vector<Proto*> all_protos;
static std::set<std::string> all_strings;
static std::set<std::string> globals_read;
static std::set<std::string> globals_write;

static void collect_info(Proto* p) {
    all_protos.push_back(p);

    for (int i = 0; i < p->sizek; i++) {
        TValue* k = &p->k[i];
        if (ttisstring(k)) {
            all_strings.insert(std::string(getstr(tsvalue(k))));
        }
    }

    AlccInstruction dec;
    for (int i = 0; i < p->sizecode; i++) {
        current_backend_ptr->decode_instruction((uint32_t)p->code[i], &dec);

        // Find global access
        if (dec.op == OP_GETTABUP) {
            // b is upvalue, c is key
            if (dec.b < p->sizeupvalues) {
                Upvaldesc* u = &p->upvalues[dec.b];
                if ((u->name && strcmp(getstr(u->name), "_ENV") == 0) || dec.b == 0) { // Fallback to upvalue 0
                    int c_idx = dec.c;
                    if (ISK(c_idx)) {
                        c_idx = INDEXK(c_idx);
                    }
                    if (c_idx < p->sizek) {
                        TValue* k = &p->k[c_idx];
                        if (ttisstring(k)) {
                            globals_read.insert(std::string(getstr(tsvalue(k))));
                        }
                    }
                }
            }
        } else if (dec.op == OP_SETTABUP) {
            // a is upvalue, b is key
            if (dec.a < p->sizeupvalues) {
                Upvaldesc* u = &p->upvalues[dec.a];
                if ((u->name && strcmp(getstr(u->name), "_ENV") == 0) || dec.a == 0) {
                    int b_idx = dec.b;
                    if (ISK(b_idx)) {
                        b_idx = INDEXK(b_idx);
                    }
                    if (b_idx < p->sizek) {
                        TValue* k = &p->k[b_idx];
                        if (ttisstring(k)) {
                            globals_write.insert(std::string(getstr(tsvalue(k))));
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < p->sizep; i++) {
        collect_info(p->p[i]);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.luac\n", argv[0]);
        return 1;
    }

    current_backend_ptr = current_backend;

    const char* input_file = argv[1];

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        return 1;
    }

    StkId o = ALCC_TOP(L) - 1;
    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    collect_info(p);

    printf("=== Functions Window ===\n");
    for (size_t i = 0; i < all_protos.size(); i++) {
        Proto* f = all_protos[i];
        printf("  [%zu] %p - lines %d-%d, %d params, %d code bytes\n",
            i, f, f->linedefined, f->lastlinedefined, f->numparams, f->sizecode);
    }

    printf("\n=== Strings Window ===\n");
    for (const auto& str : all_strings) {
        printf("  \"%s\"\n", str.c_str());
    }

    printf("\n=== Imports (Globals Read) ===\n");
    for (const auto& g : globals_read) {
        printf("  %s\n", g.c_str());
    }

    printf("\n=== Exports (Globals Written) ===\n");
    for (const auto& g : globals_write) {
        printf("  %s\n", g.c_str());
    }

    lua_close(L);
    return 0;
}
