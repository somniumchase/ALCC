#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstring.h"
#include "lmem.h"
#include "lopnames.h"

// Helper to skip whitespace
static char* skip_space(char* s) {
    while (*s && isspace(*s)) s++;
    return s;
}

// Parse quoted string unescaping
static char* parse_string(char* s, char* buffer) {
    s = skip_space(s);
    if (*s != '"') return NULL;
    s++;
    char* d = buffer;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            if (*s == 'n') *d++ = '\n';
            else if (*s == 'r') *d++ = '\r';
            else if (*s == 't') *d++ = '\t';
            else if (*s == '\\') *d++ = '\\';
            else if (*s == '"') *d++ = '"';
            else if (*s == 'x') {
                int h;
                if (sscanf(s+1, "%02x", &h) == 1) {
                    *d++ = (char)h;
                    s += 2;
                } else {
                    *d++ = 'x';
                }
            } else {
                *d++ = *s;
            }
        } else {
            *d++ = *s;
        }
        s++;
    }
    *d = '\0';
    if (*s == '"') s++;
    return s;
}

static Proto* parse_proto(lua_State* L, FILE* f) {
    char line[4096];
    Proto* p = luaF_newproto(L);

    // Parse NumParams
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "; NumParams:")) break;
    }

    int numparams=0, is_vararg=0, maxstacksize=2;
    char* s_head = strstr(line, "; NumParams:");
    if (s_head) sscanf(s_head, "; NumParams: %d, IsVararg: %d, MaxStackSize: %d",
           &numparams, &is_vararg, &maxstacksize);
    p->numparams = (lu_byte)numparams;
    p->maxstacksize = (lu_byte)maxstacksize;
    p->flag |= (lu_byte)is_vararg;

    // Parse Upvalues
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "; Upvalues")) break;
    }
    int nup=0;
    char* s_up = strstr(line, "; Upvalues");
    if (s_up) sscanf(s_up, "; Upvalues (%d):", &nup);
    p->sizeupvalues = nup;
    if (nup > 0) {
        p->upvalues = luaM_newvector(L, nup, Upvaldesc);
        for (int i=0; i<nup; i++) {
            p->upvalues[i].name = NULL;
            p->upvalues[i].instack = 0;
            p->upvalues[i].idx = 0;
            p->upvalues[i].kind = 0;
        }

        for (int i=0; i<nup; i++) {
            if (!fgets(line, sizeof(line), f)) break;
            char* s = strchr(line, ']');
            if (!s) continue;
            s++;
            char namebuf[1024];
            char* after_name;

            s = skip_space(s);
            if (*s == '"') {
                after_name = parse_string(s, namebuf);
            } else {
                // (no name)
                strcpy(namebuf, "");
                after_name = strchr(s, ')');
                if (after_name) after_name++;
                else after_name = s;
            }

            if (namebuf[0]) {
                p->upvalues[i].name = luaS_new(L, namebuf);
            }

            int instack=0, idx=0, kind=0;
            sscanf(after_name, "%d %d %d", &instack, &idx, &kind);
            p->upvalues[i].instack = (lu_byte)instack;
            p->upvalues[i].idx = (lu_byte)idx;
            p->upvalues[i].kind = (lu_byte)kind;
        }
    }

    // Parse Constants
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "; Constants")) break;
    }
    int nk=0;
    char* s_k = strstr(line, "; Constants");
    if (s_k) sscanf(s_k, "; Constants (%d):", &nk);
    p->sizek = nk;
    if (nk > 0) {
        p->k = luaM_newvector(L, nk, TValue);
        for (int i=0; i<nk; i++) setnilvalue(&p->k[i]);

        for (int i=0; i<nk; i++) {
            if (!fgets(line, sizeof(line), f)) break;
            char* s = strchr(line, ']');
            if (!s) continue;
            s++;
            s = skip_space(s);
            if (*s == '"') {
                char buf[4096];
                parse_string(s, buf);
                setsvalue(L, &p->k[i], luaS_new(L, buf));
            } else if (strncmp(s, "nil", 3) == 0) {
                setnilvalue(&p->k[i]);
            } else if (strncmp(s, "true", 4) == 0) {
                setbtvalue(&p->k[i]);
            } else if (strncmp(s, "false", 5) == 0) {
                setbfvalue(&p->k[i]);
            } else {
                if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E')) {
                    lua_Number ln;
                    sscanf(s, "%lf", &ln);
                    setfltvalue(&p->k[i], ln);
                } else {
                     lua_Integer li;
                    if (sscanf(s, "%lld", &li) == 1) {
                         setivalue(&p->k[i], li);
                    } else {
                         // Fallback to float if integer parse fails (e.g. overflow or weird format)
                         lua_Number ln;
                         sscanf(s, "%lf", &ln);
                         setfltvalue(&p->k[i], ln);
                    }
                }
            }
        }
    }

    // Parse Code
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "; Code")) break;
    }
    int ncode=0;
    char* s_code = strstr(line, "; Code");
    if (s_code) sscanf(s_code, "; Code (%d):", &ncode);
    p->sizecode = ncode;
    if (ncode > 0) {
        p->code = luaM_newvector(L, ncode, Instruction);

        for (int i=0; i<ncode; i++) {
            if (!fgets(line, sizeof(line), f)) break;
            char* s = strchr(line, ']');
            if (!s) continue;
            s++;
            char opname[32];
            sscanf(s, "%s", opname);

            OpCode op = 0;
            int found = 0;
            for (int j=0; opnames[j]; j++) {
                if (strcmp(opnames[j], opname) == 0) {
                    op = (OpCode)j;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "Unknown opcode: %s\n", opname);
                continue;
            }

            s = strstr(s, opname) + strlen(opname);

            int args[10];
            int nargs = 0;
            char* ptr = s;
            int has_k = 0;
            if (strstr(line, "(k)")) has_k = 1;

            while (*ptr) {
                while (*ptr && !isdigit(*ptr) && *ptr != '-') {
                     if (*ptr == '\n') break;
                     ptr++;
                }
                if (!*ptr || *ptr == '\n') break;

                // Check if it's part of "(k)" or "; comment"
                // Actually my disassembler output: "LOADK 1 1 ; ..."
                // The comment is after a ';'.
                // So I should stop at ';'
                // Wait, my loop above:
                // I need to strip comments first.
                // But my `parse_string` logic might be tricky if `;` is inside string.
                // But here I am parsing args (integers).

                int val;
                if (sscanf(ptr, "%d", &val) == 1) {
                    args[nargs++] = val;
                    if (*ptr == '-') ptr++;
                    while (isdigit(*ptr)) ptr++;
                } else {
                    ptr++;
                }
            }

            Instruction inst = 0;
            SET_OPCODE(inst, op);
            SETARG_A(inst, 0);

            switch (getOpMode(op)) {
                case iABC:
                    if (nargs >= 1) SETARG_A(inst, args[0]);
                    if (nargs >= 2) SETARG_B(inst, args[1]);
                    if (nargs >= 3) SETARG_C(inst, args[2]);
                    if (has_k) SETARG_k(inst, 1);
                    break;
                case ivABC:
                     if (nargs >= 1) SETARG_A(inst, args[0]);
                     if (nargs >= 2) SETARG_vB(inst, args[1]);
                     if (nargs >= 3) SETARG_vC(inst, args[2]);
                     if (has_k) SETARG_k(inst, 1);
                     break;
                case iABx:
                    if (nargs >= 1) SETARG_A(inst, args[0]);
                    if (nargs >= 2) SETARG_Bx(inst, args[1]);
                    break;
                case iAsBx:
                    if (nargs >= 1) SETARG_A(inst, args[0]);
                    if (nargs >= 2) SETARG_sBx(inst, args[1]);
                    break;
                case iAx:
                    if (nargs >= 1) SETARG_Ax(inst, args[0]);
                    break;
                case isJ:
                    if (nargs >= 1) SETARG_sJ(inst, args[0]);
                    if (has_k) SETARG_k(inst, 1);
                    break;
            }
            p->code[i] = inst;
        }
    }

    // Parse Protos
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "; Protos")) break;
    }
    int np=0;
    char* s_p = strstr(line, "; Protos");
    if (s_p) sscanf(s_p, "; Protos (%d):", &np);
    p->sizep = np;
    if (np > 0) {
        p->p = luaM_newvector(L, np, Proto*);
        for (int i=0; i<np; i++) {
            p->p[i] = parse_proto(L, f);
        }
    }

    return p;
}

static int writer(lua_State* L, const void* p, size_t sz, void* ud) {
    (void)L;
    return (fwrite(p, sz, 1, (FILE*)ud) != 1) && (sz != 0);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.asm -o output.luac\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[i+1];
        }
    }

    if (!output_file) {
        fprintf(stderr, "Output file required (-o output.luac)\n");
        return 1;
    }

    FILE* f = fopen(input_file, "r");
    if (!f) {
        fprintf(stderr, "Cannot open input file %s\n", input_file);
        return 1;
    }

    lua_State* L = luaL_newstate();
    if (!L) return 1;
    lua_gc(L, LUA_GCSTOP, 0); // Stop GC to prevent collection of unanchored protos

    Proto* p = parse_proto(L, f);
    fclose(f);

    // Create LClosure to dump
    // We need to push it to stack.
    // LClosure needs a GCObject.
    // luaF_newLclosure(L, nup)
    LClosure* cl = luaF_newLclosure(L, 1); // 1 upvalue (_ENV)
    cl->p = p;
    // Set upvalue 0 to _ENV?
    // Usually main chunk has _ENV as upvalue 0.
    // But lua_dump doesn't really care about the value of upvalue, only the name in Proto for debug.
    // However, lua_dump expects top of stack to be the function.

    setclLvalue2s(L, L->top.p, cl);
    L->top.p++;

    FILE* fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Cannot open output file %s\n", output_file);
        return 1;
    }

    if (lua_dump(L, writer, fout, 0) != 0) {
        fprintf(stderr, "Error dumping chunk\n");
    }

    fclose(fout);
    lua_close(L);
    return 0;
}
