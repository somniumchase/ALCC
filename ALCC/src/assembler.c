#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstring.h"
#include "lmem.h"
#include "alcc_utils.h"
#include "alcc_backend.h"
#include "../plugin/alcc_plugin.h"

typedef struct {
    FILE* f;
    int line_no;
    char buffer[4096];
    AlccPlugin* plugin;
} AssemblerCtx;

static char* get_line(ParseCtx* ctx, AlccPlugin* plugin) {
    if (!fgets(ctx->buffer, sizeof(ctx->buffer), ctx->f)) return NULL;
    ctx->line_no++;
    size_t len = strlen(ctx->buffer);
    if (len > 0 && ctx->buffer[len-1] == '\n') ctx->buffer[len-1] = '\0';

    if (plugin && plugin->on_asm_line) {
        plugin->on_asm_line(ctx, ctx->buffer);
    }

    return ctx->buffer;
}

static void parse_error(ParseCtx* ctx, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error at line %d: ", ctx->line_no);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static char* find_line_starting_with(ParseCtx* ctx, AlccPlugin* plugin, const char* prefix) {
    while (get_line(ctx, plugin)) {
        char* s = alcc_skip_space(ctx->buffer);
        if (strncmp(s, prefix, strlen(prefix)) == 0) return s;
    }
    return NULL;
}

static Proto* parse_proto(lua_State* L, ParseCtx* ctx, AlccPlugin* plugin) {
    Proto* p = luaF_newproto(L);

    char* line = find_line_starting_with(ctx, plugin, "; NumParams:");
    if (!line) parse_error(ctx, "Expected '; NumParams:'");

    int numparams=0, is_vararg=0, maxstacksize=2;
    if (sscanf(line, "; NumParams: %d, IsVararg: %d, MaxStackSize: %d",
           &numparams, &is_vararg, &maxstacksize) != 3) {
        parse_error(ctx, "Invalid NumParams format");
    }
    p->numparams = (lu_byte)numparams;
    p->maxstacksize = (lu_byte)maxstacksize;
    p->flag |= (lu_byte)is_vararg;

    // Upvalues
    line = find_line_starting_with(ctx, plugin, "; Upvalues");
    if (!line) parse_error(ctx, "Expected '; Upvalues'");

    int nup=0;
    sscanf(line, "; Upvalues (%d):", &nup);
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
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing upvalues");
            char* s = strchr(ctx->buffer, ']');
            if (!s) continue;
            s++;
            char namebuf[1024];
            char* after_name;

            s = alcc_skip_space(s);
            if (*s == '"') {
                after_name = alcc_parse_string(s, namebuf);
            } else {
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

    // Constants
    line = find_line_starting_with(ctx, plugin, "; Constants");
    if (!line) parse_error(ctx, "Expected '; Constants'");

    int nk=0;
    sscanf(line, "; Constants (%d):", &nk);
    p->sizek = nk;
    if (nk > 0) {
        p->k = luaM_newvector(L, nk, TValue);
        for (int i=0; i<nk; i++) setnilvalue(&p->k[i]);

        for (int i=0; i<nk; i++) {
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing constants");
            char* s = strchr(ctx->buffer, ']');
            if (!s) continue;
            s++;
            s = alcc_skip_space(s);

            if (*s == '"') {
                char buf[4096];
                alcc_parse_string(s, buf);
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
                         lua_Number ln;
                         sscanf(s, "%lf", &ln);
                         setfltvalue(&p->k[i], ln);
                    }
                }
            }
        }
    }

    // Code
    line = find_line_starting_with(ctx, plugin, "; Code");
    if (!line) parse_error(ctx, "Expected '; Code'");

    int ncode=0;
    sscanf(line, "; Code (%d):", &ncode);
    p->sizecode = ncode;
    if (ncode > 0) {
        p->code = luaM_newvector(L, ncode, Instruction);

        for (int i=0; i<ncode; i++) {
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing code");
            char* s = strchr(ctx->buffer, ']');
            if (!s) continue;
            s++;

            char opname[32];
            if (sscanf(s, "%31s", opname) != 1) parse_error(ctx, "Cannot parse opcode");

            // Abstraction Lookup using Backend
            int found_op = -1;
            const AlccOpInfo* info = NULL;
            int num_ops = current_backend->get_op_count();

            for (int j=0; j<num_ops; j++) {
                const AlccOpInfo* inf = current_backend->get_op_info(j);
                if (inf && strcmp(inf->name, opname) == 0) {
                    found_op = j;
                    info = inf;
                    break;
                }
            }

            if (found_op < 0 || !info) {
                parse_error(ctx, "Unknown opcode: %s", opname);
            }

            s = strstr(s, opname);
            if (!s) parse_error(ctx, "Internal error parsing opname");
            s += strlen(opname);

            int args[10];
            int nargs = 0;
            char* ptr = s;
            int has_k = 0;
            if (strstr(ctx->buffer, "(k)")) has_k = 1;

            while (*ptr) {
                while (*ptr && !isdigit(*ptr) && *ptr != '-') {
                     if (*ptr == '\0') break;
                     // Stop if comment
                     if (*ptr == ';') { *ptr = '\0'; break; }
                     ptr++;
                }
                if (!*ptr) break;

                int val;
                if (sscanf(ptr, "%d", &val) == 1) {
                    args[nargs++] = val;
                    if (*ptr == '-') ptr++;
                    while (isdigit(*ptr)) ptr++;
                } else {
                    ptr++;
                }
            }

            AlccInstruction enc;
            enc.op = found_op;
            enc.a = (nargs >= 1) ? args[0] : 0;
            enc.b = 0; enc.c = 0; enc.k = 0; enc.bx = 0;

            // Map args based on mode
            switch (info->mode) {
                case ALCC_iABC:
                case ALCC_ivABC:
                    if (nargs >= 2) enc.b = args[1];
                    if (nargs >= 3) enc.c = args[2];
                    if (has_k) enc.k = 1;
                    break;
                case ALCC_iABx:
                case ALCC_iAsBx:
                case ALCC_iAx:
                case ALCC_isJ:
                    if (nargs >= 2) enc.bx = args[1];
                    if (has_k && info->mode == ALCC_isJ) enc.k = 1;
                    if (info->mode == ALCC_iAx && nargs >= 1) enc.bx = args[0];
                    if (info->mode == ALCC_isJ && nargs >= 1) enc.bx = args[0];
                    break;
            }

            p->code[i] = (Instruction)current_backend->encode_instruction(&enc);
        }
    }

    // Protos
    line = find_line_starting_with(ctx, plugin, "; Protos");
    if (!line) parse_error(ctx, "Expected '; Protos'");

    int np=0;
    sscanf(line, "; Protos (%d):", &np);
    p->sizep = np;
    if (np > 0) {
        p->p = luaM_newvector(L, np, Proto*);
        for (int i=0; i<np; i++) {
            p->p[i] = parse_proto(L, ctx, plugin);
        }
    }

    return p;
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

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    ParseCtx ctx;
    ctx.f = f;
    ctx.line_no = 0;

    Proto* p = parse_proto(L, &ctx, NULL);
    fclose(f);

    LClosure* cl = luaF_newLclosure(L, 1);
    cl->p = p;

    setclLvalue2s(L, L->top.p, cl);
    L->top.p++;

    FILE* fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Cannot open output file %s\n", output_file);
        return 1;
    }

    if (lua_dump(L, alcc_writer, fout, 0) != 0) {
        fprintf(stderr, "Error dumping chunk\n");
    }

    fclose(fout);
    lua_close(L);
    return 0;
}
