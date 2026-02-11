#include "Template2.h"
#include "../core/compat.h"
#include "DecompilerCore.h"
#include <iostream>

void Template2::decompile(Proto* p, int level, AlccPlugin* plugin) {
    DecompilerCore::decompile(p, level, plugin);
}

void Template2::disassemble(Proto* p, AlccPlugin* plugin) {
    print_proto(p, 0, plugin);
}

void Template2::print_code(Proto* p, int level, AlccPlugin* plugin) {
    char buffer[4096];
    AlccInstruction dec;

    for (int i = 0; i < p->sizecode; i++) {
        Instruction inst = p->code[i];

        current_backend->decode_instruction((uint32_t)inst, &dec);
        const AlccOpInfo* info = current_backend->get_op_info(dec.op);

        printf("%*s  ", level*2, "");

        // Plugin Hook
        if (plugin && plugin->on_instruction) {
            if (plugin->on_instruction(p, i, buffer, sizeof(buffer))) {
                printf("%s\n", buffer);
                continue;
            }
        }

        if (!info) {
            printf("UNKNOWN(%d)\n", dec.op);
            continue;
        }

        printf("%s", info->name);

        switch (info->mode) {
            case ALCC_iABC:
                printf(" %d %d %d", dec.a, dec.b, dec.c);
                if (info->has_k && dec.k) printf(" k");
                break;
            case ALCC_ivABC:
                printf(" %d %d %d", dec.a, dec.b, dec.c);
                if (info->has_k && dec.k) printf(" k");
                break;
            case ALCC_iABx:
                printf(" %d %d", dec.a, dec.bx);
                break;
            case ALCC_iAsBx:
                printf(" %d %d", dec.a, dec.bx);
                break;
            case ALCC_iAx:
                printf(" %d", dec.bx);
                break;
            case ALCC_isJ:
                printf(" %d", dec.bx);
                if (info->has_k && dec.k) printf(" k");
                break;
        }

        // Comments for constants (same as default)
        if (dec.op == OP_LOADK) {
            int bx = dec.bx;
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

void Template2::print_proto(Proto* p, int level, AlccPlugin* plugin) {
    if (plugin && plugin->on_disasm_header) {
        plugin->on_disasm_header(p);
    }

    printf("%*s.fn func_%p\n", level*2, "", p);

    // Upvalues
    printf("%*s..upvalues %d\n", level*2, "", p->sizeupvalues);
    for (int i = 0; i < p->sizeupvalues; i++) {
        Upvaldesc* u = &p->upvalues[i];
        printf("%*s  ", level*2, "");
        if (u->name) alcc_print_string(getstr(u->name), tsslen(u->name));
        else printf("\"\"");
        printf(" %d %d %d\n", u->instack, u->idx, ALCC_UPVAL_KIND_GET(u));
    }

    // Args
    printf("%*s..args %d %d %d\n", level*2, "", p->numparams, isvararg(p), p->maxstacksize);

    // Constants
    printf("%*s..consts %d\n", level*2, "", p->sizek);
    for (int i = 0; i < p->sizek; i++) {
        TValue* k = &p->k[i];
        printf("%*s  ", level*2, "");
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

    // Code
    // Note: Template 2 example did not explicitly show a code header, just indented code.
    // But we need to know the size for assembly if we pre-allocate.
    // The example had "..args", then instructions.
    // I will add a directive for code start/size to be safe, or just start instructions.
    // But to parse back efficiently, a header is nice.
    // Let's assume standard sections.
    printf("%*s..code %d\n", level*2, "", p->sizecode);
    print_code(p, level, plugin);

    // Protos
    printf("%*s..protos %d\n", level*2, "", p->sizep);
    for (int i = 0; i < p->sizep; i++) {
        print_proto(p->p[i], level+1, plugin);
    }

    printf("%*s.end\n", level*2, "");
}

Proto* Template2::assemble(lua_State* L, ParseCtx* ctx, AlccPlugin* plugin) {
    Proto* p = luaF_newproto(L);

    char* line = find_line_starting_with(ctx, plugin, ".fn");
    if (!line) parse_error(ctx, "Expected '.fn'");

    // Upvalues
    line = find_line_starting_with(ctx, plugin, "..upvalues");
    if (!line) parse_error(ctx, "Expected '..upvalues'");
    int nup=0;
    sscanf(line, "..upvalues %d", &nup);
    p->sizeupvalues = nup;
    if (nup > 0) {
        p->upvalues = luaM_newvector(L, nup, Upvaldesc);
        for (int i=0; i<nup; i++) {
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing upvalues");
            char* s = alcc_skip_space(ctx->buffer);
            std::string namebuf;
            char* after_name;

            if (*s == '"') {
                after_name = alcc_parse_string(s, namebuf);
            } else {
                parse_error(ctx, "Expected quoted name for upvalue");
                return NULL;
            }

            if (!namebuf.empty()) {
                p->upvalues[i].name = luaS_new(L, namebuf.c_str());
            } else {
                p->upvalues[i].name = NULL;
            }

            int instack=0, idx=0, kind=0;
            sscanf(after_name, "%d %d %d", &instack, &idx, &kind);
            p->upvalues[i].instack = (lu_byte)instack;
            p->upvalues[i].idx = (lu_byte)idx;
            ALCC_UPVAL_KIND_SET(&p->upvalues[i], (lu_byte)kind);
        }
    }

    // Args
    line = find_line_starting_with(ctx, plugin, "..args");
    if (!line) parse_error(ctx, "Expected '..args'");
    int numparams=0, is_vararg=0, maxstacksize=2;
    sscanf(line, "..args %d %d %d", &numparams, &is_vararg, &maxstacksize);
    p->numparams = (lu_byte)numparams;
    p->maxstacksize = (lu_byte)maxstacksize;
    ALCC_SET_VARARG(p, (lu_byte)is_vararg);

    // Constants
    line = find_line_starting_with(ctx, plugin, "..consts");
    if (!line) parse_error(ctx, "Expected '..consts'");
    int nk=0;
    sscanf(line, "..consts %d", &nk);
    p->sizek = nk;
    if (nk > 0) {
        p->k = luaM_newvector(L, nk, TValue);
        for (int i=0; i<nk; i++) setnilvalue(&p->k[i]);

        for (int i=0; i<nk; i++) {
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing constants");
            char* s = alcc_skip_space(ctx->buffer);

            if (*s == '"') {
                std::string buf;
                alcc_parse_string(s, buf);
                setsvalue(L, &p->k[i], luaS_new(L, buf.c_str()));
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
    line = find_line_starting_with(ctx, plugin, "..code");
    if (!line) parse_error(ctx, "Expected '..code'");
    int ncode=0;
    sscanf(line, "..code %d", &ncode);
    p->sizecode = ncode;
    if (ncode > 0) {
        p->code = luaM_newvector(L, ncode, Instruction);
        for (int i=0; i<ncode; i++) {
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing code");
            char* s = alcc_skip_space(ctx->buffer);

            char opname[32];
            if (sscanf(s, "%31s", opname) != 1) parse_error(ctx, "Cannot parse opcode");

            // Abstraction Lookup
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
            // Template 2 uses 'k' suffix? "LOADK 0 1 k" or something?
            // My print logic puts " k" at the end if has_k.
            if (strstr(ctx->buffer, " k")) has_k = 1;

            while (*ptr) {
                while (*ptr && !isdigit(*ptr) && *ptr != '-') {
                     if (*ptr == '\0') break;
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
    line = find_line_starting_with(ctx, plugin, "..protos");
    if (!line) parse_error(ctx, "Expected '..protos'");
    int np=0;
    sscanf(line, "..protos %d", &np);
    p->sizep = np;
    if (np > 0) {
        p->p = luaM_newvector(L, np, Proto*);
        for (int i=0; i<np; i++) {
            p->p[i] = assemble(L, ctx, plugin);
        }
    }

    // End
    line = find_line_starting_with(ctx, plugin, ".end");
    if (!line) parse_error(ctx, "Expected '.end'");

    return p;
}

char* Template2::get_line(ParseCtx* ctx, AlccPlugin* plugin) {
    if (!fgets(ctx->buffer, sizeof(ctx->buffer), ctx->f)) return NULL;
    ctx->line_no++;
    size_t len = strlen(ctx->buffer);
    if (len > 0 && ctx->buffer[len-1] == '\n') ctx->buffer[len-1] = '\0';

    if (plugin && plugin->on_asm_line) {
        plugin->on_asm_line(ctx, ctx->buffer);
    }

    return ctx->buffer;
}

void Template2::parse_error(ParseCtx* ctx, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error at line %d: ", ctx->line_no);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

char* Template2::find_line_starting_with(ParseCtx* ctx, AlccPlugin* plugin, const char* prefix) {
    while (get_line(ctx, plugin)) {
        char* s = alcc_skip_space(ctx->buffer);
        if (strncmp(s, prefix, strlen(prefix)) == 0) return s;
    }
    return NULL;
}
