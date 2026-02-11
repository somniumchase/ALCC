#include "DefaultTemplate.h"
#include "../core/compat.h"
#include "DecompilerCore.h"
#include <iostream>
#include <string.h>
#include <set>
#include <vector>
#include <algorithm>
#include <map>
#include <string>

extern "C" {
#include "lfunc.h"
#include "lstring.h"
#include "lopcodes.h"
}

void DefaultTemplate::decompile(Proto* p, int level, AlccPlugin* plugin) {
    DecompilerCore::decompile(p, level, plugin);
}

void DefaultTemplate::disassemble(Proto* p, AlccPlugin* plugin) {
    print_proto(p, 0, plugin);
}

static void analyze_jump_targets(Proto* p, std::set<int>& targets) {
    AlccInstruction dec;
    for (int i = 0; i < p->sizecode; i++) {
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
            targets.insert(target);
        }
    }
}

void DefaultTemplate::print_code(Proto* p, int level, AlccPlugin* plugin) {
    char buffer[4096];
    AlccInstruction dec;
    std::set<int> targets;
    analyze_jump_targets(p, targets);

    for (int i = 0; i < p->sizecode; i++) {
        if (targets.count(i)) {
            printf("%*sL_%d:\n", level*2, "", i + 1);
        }

        Instruction inst = p->code[i];

        current_backend->decode_instruction((uint32_t)inst, &dec);
        const AlccOpInfo* info = current_backend->get_op_info(dec.op);

        printf("%*s[%03d] ", level*2, "", i+1);

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

        printf("%-12s", info->name);

        switch (info->mode) {
            case ALCC_iABC:
                printf("%d %d %d", dec.a, dec.b, dec.c);
                if (info->has_k && dec.k) printf(" (k)");
                break;
            case ALCC_ivABC:
                printf("%d %d %d", dec.a, dec.b, dec.c);
                if (info->has_k && dec.k) printf(" (k)");
                break;
            case ALCC_iABx:
                printf("%d %d", dec.a, dec.bx);
                break;
            case ALCC_iAsBx:
                printf("%d %d", dec.a, dec.bx);
                break;
            case ALCC_iAx:
                printf("%d", dec.bx);
                break;
            case ALCC_isJ:
                printf("%d", dec.bx);
                if (info->has_k && dec.k) printf(" (k)");
                break;
        }

        // Comments for constants (Existing Logic)
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

        // NEW: Annotate Variables and Upvalues
        char comment_buf[512];
        comment_buf[0] = '\0';

        auto append_var = [&](int reg, const char* label) {
            const char* name = luaF_getlocalname(p, reg + 1, i); // PC is i?
            // luaF_getlocalname takes PC of *current* instruction?
            // Usually valid at PC+1? Or PC?
            // Lua debug info ranges are [startpc, endpc].
            // If i is inside range, it returns name.
            if (name) {
                if (comment_buf[0] == '\0') strcpy(comment_buf, " ; ");
                else strcat(comment_buf, " ");
                char tmp[128];
                snprintf(tmp, sizeof(tmp), "%sR[%d]:%s", label, reg, name);
                strcat(comment_buf, tmp);
            }
        };

        auto append_upval = [&](int uv, const char* label) {
            if (uv < p->sizeupvalues) {
                Upvaldesc* u = &p->upvalues[uv];
                if (u->name) {
                    if (comment_buf[0] == '\0') strcpy(comment_buf, " ; ");
                    else strcat(comment_buf, " ");
                    char tmp[128];
                    snprintf(tmp, sizeof(tmp), "%sU[%d]:%s", label, uv, getstr(u->name));
                    strcat(comment_buf, tmp);
                }
            }
        };

        // Analyze operands based on OpCode/Mode to find Registers and Upvalues
        // We use 'dec' struct which decoded A, B, C, Bx, etc.

        // Registers A is usually destination or source
        if (info->mode == ALCC_iABC || info->mode == ALCC_ivABC || info->mode == ALCC_iABx || info->mode == ALCC_iAsBx) {
            append_var(dec.a, "");
        }

        // B and C
        if (info->mode == ALCC_iABC || info->mode == ALCC_ivABC) {
             // Check if B/C are registers or constants using 'k' bit if applicable?
             // But 'k' bit depends on OpCode semantics.
             // Generally, if operand is register, we check name.
             // If constant, we check value?
             // Simplest is to try checking name for B and C if they look like register indices.
             // But for 'k' operands, B/C are constant indices. checking localname might fail or give wrong result?
             // No, constant index 256 might overlap with register 0? No.
             // But register index 0 overlaps with constant index 0?
             // Register indices are small. Constant indices can be large.
             // But if B=0 (constant 0), checking register 0 name "a" -> "B:a" is wrong.
             // So we must respect 'k'.

             // OP_ADDI: A B sC. B is register. C is immediate.
             // OP_EQ: A B k. A, B registers. k bit means logic inversion? No.
             // Lua 5.4: EQ A B k.

             // We can't implement full semantic check for all opcodes here without huge switch.
             // Heuristic: If 'k' bit is NOT set (or op doesn't use it for that operand), check register.
             // But 'dec.k' is just the K bit.
             // It doesn't tell us WHICH operand is K.

             // Let's rely on specific opcodes for Upvalues, and generic A for registers.
             // And maybe B/C for arithmetic?
             // OP_ADD: A B C. All registers.
             if (dec.op == OP_ADD || dec.op == OP_SUB || dec.op == OP_MUL || dec.op == OP_DIV ||
                 dec.op == OP_IDIV || dec.op == OP_MOD || dec.op == OP_POW ||
                 dec.op == OP_BAND || dec.op == OP_BOR || dec.op == OP_BXOR ||
                 dec.op == OP_SHL || dec.op == OP_SHR || dec.op == OP_UNM ||
                 dec.op == OP_BNOT || dec.op == OP_NOT || dec.op == OP_LEN ||
                 dec.op == OP_CONCAT || dec.op == OP_MOVE) {
                 if (dec.b < 255) append_var(dec.b, "");
                 if (info->mode == ALCC_iABC && dec.c < 255 && dec.op != OP_MOVE && dec.op != OP_UNM && dec.op != OP_BNOT && dec.op != OP_NOT && dec.op != OP_LEN)
                    append_var(dec.c, "");
             }
        }

        // Upvalues
        if (dec.op == OP_GETUPVAL || dec.op == OP_SETUPVAL) {
            append_upval(dec.b, "");
        }
        else if (dec.op == OP_GETTABUP) {
            append_upval(dec.b, ""); // Table
            // C is key (K or R)
        }
        else if (dec.op == OP_SETTABUP) {
            append_upval(dec.a, ""); // Table
            // B is key (K or R)
        }
        else if (dec.op == OP_CLOSURE) {
             // bx is proto index?
        }

        // Immediate values
        if (dec.op == OP_ADDI) {
             // C is sC (immediate)
             char tmp[64];
             if (comment_buf[0] == '\0') strcpy(comment_buf, " ; ");
             else strcat(comment_buf, " ");
             snprintf(tmp, sizeof(tmp), "val:%d", dec.c - OFFSET_sC);
             strcat(comment_buf, tmp);
        } else if (dec.op == OP_EQI || dec.op == OP_LTI || dec.op == OP_LEI || dec.op == OP_GTI || dec.op == OP_GEI) {
             // B is sC (immediate)
             char tmp[64];
             if (comment_buf[0] == '\0') strcpy(comment_buf, " ; ");
             else strcat(comment_buf, " ");
             snprintf(tmp, sizeof(tmp), "val:%d", dec.b - OFFSET_sC);
             strcat(comment_buf, tmp);
        }

        // Jump Targets
        int target = -1;
        if (dec.op == OP_JMP) {
            target = i + 1 + dec.bx;
        } else if (dec.op == OP_FORLOOP || dec.op == OP_TFORLOOP) {
            target = i + 1 - dec.bx;
        } else if (dec.op == OP_FORPREP) {
            target = i + 1 + dec.bx + 1;
        }

        if (target >= 0) {
             char tmp[64];
             if (comment_buf[0] == '\0') strcpy(comment_buf, " ; ");
             else strcat(comment_buf, " ");
             snprintf(tmp, sizeof(tmp), "to L_%d", target + 1);
             strcat(comment_buf, tmp);
        }

        printf("%s", comment_buf);

        printf("\n");
    }
}

void DefaultTemplate::print_proto(Proto* p, int level, AlccPlugin* plugin) {
    if (plugin && plugin->on_disasm_header) {
        plugin->on_disasm_header(p);
    }

    printf("\n%*s; Function: %p (lines %d-%d)\n", level*2, "", p, p->linedefined, p->lastlinedefined);
    printf("%*s; NumParams: %d, IsVararg: %d, MaxStackSize: %d\n", level*2, "", p->numparams, isvararg(p), p->maxstacksize);

    printf("%*s; Upvalues (%d):\n", level*2, "", p->sizeupvalues);
    for (int i = 0; i < p->sizeupvalues; i++) {
        Upvaldesc* u = &p->upvalues[i];
        printf("%*s  [%d] ", level*2, "", i);
        if (u->name) alcc_print_string(getstr(u->name), tsslen(u->name));
        else printf("(no name)");
        printf(" %d %d %d\n", u->instack, u->idx, ALCC_UPVAL_KIND_GET(u));
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
    print_code(p, level, plugin);

    printf("%*s; Protos (%d):\n", level*2, "", p->sizep);
    for (int i = 0; i < p->sizep; i++) {
        print_proto(p->p[i], level+1, plugin);
    }
}

Proto* DefaultTemplate::assemble(lua_State* L, ParseCtx* ctx, AlccPlugin* plugin) {
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
    ALCC_SET_VARARG(p, (lu_byte)is_vararg);

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
             ALCC_UPVAL_KIND_SET(&p->upvalues[i], 0);
        }

        for (int i=0; i<nup; i++) {
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing upvalues");
            char* s = strchr(ctx->buffer, ']');
            if (!s) continue;
            s++;
            std::string namebuf;
            char* after_name;

            s = alcc_skip_space(s);
            if (*s == '"') {
                after_name = alcc_parse_string(s, namebuf);
            } else {
                namebuf.clear();
                after_name = strchr(s, ')');
                if (after_name) after_name++;
                else after_name = s;
            }

            if (!namebuf.empty()) {
                p->upvalues[i].name = luaS_new(L, namebuf.c_str());
            }

            int instack=0, idx=0, kind=0;
            sscanf(after_name, "%d %d %d", &instack, &idx, &kind);
            p->upvalues[i].instack = (lu_byte)instack;
            p->upvalues[i].idx = (lu_byte)idx;
            ALCC_UPVAL_KIND_SET(&p->upvalues[i], (lu_byte)kind);
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
    line = find_line_starting_with(ctx, plugin, "; Code");
    if (!line) parse_error(ctx, "Expected '; Code'");

    int ncode=0;
    sscanf(line, "; Code (%d):", &ncode);
    p->sizecode = ncode;
    if (ncode > 0) {
        p->code = luaM_newvector(L, ncode, Instruction);

        std::map<std::string, int> labels;
        struct PendingPatch {
            int pc;
            int arg_idx; // 0=A, 1=B, 2=C/Bx (actually just index in args array logic)
            std::string label;
        };
        std::vector<PendingPatch> patches;

        int pc = 0;
        while (pc < ncode) {
            if (!get_line(ctx, plugin)) parse_error(ctx, "Unexpected EOF while parsing code");
            char* s = alcc_skip_space(ctx->buffer);
            if (*s == '\0' || *s == ';') continue;

            // Check for label definition: L_123:
            if (strncmp(s, "L_", 2) == 0) {
                char* colon = strchr(s, ':');
                if (colon) {
                    *colon = '\0';
                    labels[std::string(s)] = pc;
                    s = colon + 1;
                    s = alcc_skip_space(s);
                    if (*s == '\0' || *s == ';') continue;
                }
            }

            char* bracket = strchr(s, ']');
            if (!bracket) continue;
            s = bracket + 1;

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
            std::string arg_labels[10];
            int nargs = 0;
            char* ptr = s;
            int has_k = 0;
            if (strstr(ctx->buffer, "(k)")) has_k = 1;

            while (*ptr) {
                while (*ptr && !isdigit(*ptr) && *ptr != '-' && *ptr != 'L') {
                     if (*ptr == '\0') break;
                     // Stop if comment
                     if (*ptr == ';') { *ptr = '\0'; break; }
                     ptr++;
                }
                if (!*ptr) break;

                if (strncmp(ptr, "L_", 2) == 0) {
                     char* end = ptr + 2;
                     while (isdigit(*end)) end++;
                     std::string lbl(ptr, end - ptr);
                     arg_labels[nargs] = lbl;
                     args[nargs++] = 0; // Placeholder
                     ptr = end;
                } else {
                    int val;
                    if (sscanf(ptr, "%d", &val) == 1) {
                        args[nargs++] = val;
                        if (*ptr == '-') ptr++;
                        while (isdigit(*ptr)) ptr++;
                    } else {
                        ptr++;
                    }
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
                    if (nargs >= 2) {
                        enc.bx = args[1];
                        if (!arg_labels[1].empty()) patches.push_back({pc, 1, arg_labels[1]});
                    }
                    if (has_k && info->mode == ALCC_isJ) enc.k = 1;
                    if (info->mode == ALCC_iAx && nargs >= 1) {
                        enc.bx = args[0];
                        if (!arg_labels[0].empty()) patches.push_back({pc, 0, arg_labels[0]});
                    }
                    if (info->mode == ALCC_isJ && nargs >= 1) {
                         // isJ usually JMP A sJ.
                         if (nargs >= 2) {
                             enc.bx = args[1];
                             if (!arg_labels[1].empty()) patches.push_back({pc, 1, arg_labels[1]});
                         } else {
                             enc.bx = args[0];
                             if (!arg_labels[0].empty()) patches.push_back({pc, 0, arg_labels[0]});
                         }
                    }
                    break;
            }

            p->code[pc] = (Instruction)current_backend->encode_instruction(&enc);
            pc++;
        }

        // Apply patches
        for (const auto& patch : patches) {
             if (labels.find(patch.label) == labels.end()) {
                 parse_error(ctx, "Undefined label: %s", patch.label.c_str());
             }
             int target = labels[patch.label];
             int offset = 0;

             AlccInstruction dec;
             current_backend->decode_instruction((uint32_t)p->code[patch.pc], &dec);

             int op = dec.op;
             if (op == OP_JMP) {
                 offset = target - patch.pc - 1;
                 dec.bx = offset;
             } else if (op == OP_FORLOOP || op == OP_TFORLOOP) {
                 offset = patch.pc + 1 - target;
                 dec.bx = offset;
             } else if (op == OP_FORPREP) {
                 offset = target - patch.pc - 2;
                 dec.bx = offset;
             }

             p->code[patch.pc] = (Instruction)current_backend->encode_instruction(&dec);
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
            p->p[i] = assemble(L, ctx, plugin);
        }
    }

    return p;
}

char* DefaultTemplate::get_line(ParseCtx* ctx, AlccPlugin* plugin) {
    if (!fgets(ctx->buffer, sizeof(ctx->buffer), ctx->f)) return NULL;
    ctx->line_no++;
    size_t len = strlen(ctx->buffer);
    if (len > 0 && ctx->buffer[len-1] == '\n') ctx->buffer[len-1] = '\0';

    if (plugin && plugin->on_asm_line) {
        plugin->on_asm_line(ctx, ctx->buffer);
    }

    return ctx->buffer;
}

void DefaultTemplate::parse_error(ParseCtx* ctx, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error at line %d: ", ctx->line_no);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

char* DefaultTemplate::find_line_starting_with(ParseCtx* ctx, AlccPlugin* plugin, const char* prefix) {
    while (get_line(ctx, plugin)) {
        char* s = alcc_skip_space(ctx->buffer);
        if (strncmp(s, prefix, strlen(prefix)) == 0) return s;
    }
    return NULL;
}
