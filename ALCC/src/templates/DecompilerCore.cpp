#include "DecompilerCore.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstring.h"
}

#include "../core/alcc_utils.h"
#include "../core/alcc_backend.h"

#define MAX_LABELS 1000
#define BLOCK_IF 0
#define BLOCK_LOOP 1
#define BLOCK_WHILE 2
#define BLOCK_REPEAT 3

#define TARGET_NORMAL 0
#define TARGET_REPEAT 1

static int is_identifier(const char* s) {
    size_t len = strlen(s);
    if (len == 0) return 0;
    if (!isalpha((unsigned char)s[0]) && s[0] != '_') return 0;
    for (size_t i = 1; i < len; i++) {
        if (!isalnum((unsigned char)s[i]) && s[i] != '_') return 0;
    }

    static const char* keywords[] = {
        "and", "break", "do", "else", "elseif",
        "end", "false", "for", "function", "goto", "if",
        "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while",
        NULL
    };
    for (int i = 0; keywords[i]; i++) {
        if (strcmp(s, keywords[i]) == 0) return 0;
    }
    return 1;
}

// Internal structures
struct JumpAnalysis {
    int targets[MAX_LABELS];
    int types[MAX_LABELS];
    int count;
};

// Block Stack for structured control flow
struct Block {
    int target_pc; // PC where the block ends
    int start_pc;  // PC where the block starts (for loops)
    int type;      // 0: IF, 1: LOOP
};

struct BlockStack {
    Block blocks[100];
    int top;
};

// Helper functions (static to this file)

static void bs_push(BlockStack* bs, int target, int type, int start_pc = -1) {
    if (bs->top < 100) {
        bs->blocks[bs->top].target_pc = target;
        bs->blocks[bs->top].start_pc = start_pc;
        bs->blocks[bs->top].type = type;
        bs->top++;
    }
}

static int bs_check_end(BlockStack* bs, int pc, Proto* p) {
    if (bs->top > 0) {
        // For REPEAT, we don't have a fixed target_pc in the forward sense (it ends at conditional).
        // But we handle popping manually in the conditional block.
        if (bs->blocks[bs->top-1].type == BLOCK_REPEAT) return 0;

        if (bs->blocks[bs->top-1].target_pc <= pc) {
            // Check for ELSE block transition
            if (pc > 0) {
                 Instruction prev = p->code[pc-1];
                 if (GET_OPCODE(prev) == OP_JMP) {
                     int sJ = GETARG_sJ(prev);
                     int target = pc + sJ;

                     // Only treat as ELSE if it's an IF block, not WHILE/LOOP
                     if (bs->blocks[bs->top-1].type == BLOCK_IF && target > pc) {
                         bs->blocks[bs->top-1].target_pc = target;
                         return 2; // Signal ELSE
                     }
                 }
            }
            bs->top--;
            return 1; // Signal END
        }
    }
    return 0;
}

// Helper to check if a variable is marked as to-close (OP_TBC follows)
static int is_toclose(Proto* p, int pc, int reg) {
    if (pc + 1 < p->sizecode) {
        AlccInstruction next;
        current_backend->decode_instruction((uint32_t)p->code[pc+1], &next);
        if (next.op == OP_TBC && next.a == reg) return 1;
    }
    return 0;
}

static void analyze_jumps(Proto* p, JumpAnalysis* ja) {
    ja->count = 0;
    AlccInstruction dec;

    // First pass: identify all targets
    for (int i=0; i<p->sizecode; i++) {
        current_backend->decode_instruction((uint32_t)p->code[i], &dec);

        int op = dec.op;
        int target = -1;
        int type = TARGET_NORMAL;

        if (op == OP_JMP) {
            target = i + 1 + dec.bx;
        } else if (op == OP_FORLOOP || op == OP_TFORLOOP) {
            target = i + 1 - dec.bx;
        } else if (op == OP_FORPREP) {
            target = i + 1 + dec.bx + 1;
        } else if (op == OP_EQ || op == OP_LT || op == OP_LE || op == OP_EQK || op == OP_EQI ||
                   op == OP_LTI || op == OP_LEI || op == OP_GTI || op == OP_GEI ||
                   op == OP_TEST || op == OP_TESTSET) {
            // Check for backward conditional jump (repeat..until)
             if (i + 1 < p->sizecode) {
                 AlccInstruction next;
                 current_backend->decode_instruction((uint32_t)p->code[i+1], &next);
                 if (next.op == OP_JMP) {
                     int dest = i + 1 + 1 + next.bx;
                     if (dest <= i) {
                         target = dest;
                         type = TARGET_REPEAT;
                     }
                 }
             }
        }

        if (target >= 0 && target < p->sizecode) {
            int found = -1;
            for (int j=0; j<ja->count; j++) {
                if (ja->targets[j] == target) { found=j; break; }
            }
            if (found >= 0) {
                 if (type == TARGET_REPEAT) ja->types[found] = TARGET_REPEAT;
            } else if (ja->count < MAX_LABELS) {
                ja->targets[ja->count] = target;
                ja->types[ja->count] = type;
                ja->count++;
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

                tmp = ja->types[i];
                ja->types[i] = ja->types[j];
                ja->types[j] = tmp;
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

static int get_label_type(JumpAnalysis* ja, int pc) {
    for (int i=0; i<ja->count; i++) {
        if (ja->targets[i] == pc) return ja->types[i];
    }
    return TARGET_NORMAL;
}

static void print_var(Proto* p, int reg, int pc) {
    const char* name = luaF_getlocalname(p, reg + 1, pc);
    if (name) {
        printf("%s", name);
    } else {
        if (reg < p->numparams) {
            printf("P%d", reg);
        } else {
            printf("v%d", reg); // Enhanced naming
        }
    }
}

static void print_upval(Proto* p, int idx) {
    if (idx < p->sizeupvalues && p->upvalues[idx].name) {
        const char* name = getstr(p->upvalues[idx].name);
        if (is_identifier(name)) {
            printf("%s", name);
        } else {
            alcc_print_string(name, tsslen(p->upvalues[idx].name));
        }
    } else {
        printf("upval_%d", idx);
    }
}

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

// Helper to check if an instruction is a conditional jump
static int is_conditional_jump(Proto* p, int pc, int* target) {
    if (pc >= p->sizecode) return 0;
    AlccInstruction dec;
    current_backend->decode_instruction((uint32_t)p->code[pc], &dec);
    int op = dec.op;

    // Check for logical ops followed by JMP
    if (op == OP_EQ || op == OP_LT || op == OP_LE || op == OP_EQK || op == OP_EQI ||
        op == OP_LTI || op == OP_LEI || op == OP_GTI || op == OP_GEI ||
        op == OP_TEST || op == OP_TESTSET) {

        if (pc + 1 < p->sizecode) {
             AlccInstruction next;
             current_backend->decode_instruction((uint32_t)p->code[pc+1], &next);
             if (next.op == OP_JMP) {
                 if (target) *target = pc + 1 + 1 + next.bx;
                 return 1;
             }
        }
    }
    return 0;
}

void DecompilerCore::decompile(Proto* p, int level, AlccPlugin* plugin, const char* name_override) {
    JumpAnalysis ja;
    analyze_jumps(p, &ja);

    BlockStack bs;
    bs.top = 0;

    int indent = level + 1;

    printf("%*s", level*2, "");
    if (name_override) {
        printf("%s(", name_override);
    } else {
        printf("function func_%p(", p);
    }

    for (int i=0; i<p->numparams; i++) {
        if (i>0) printf(", ");
        const char* name = luaF_getlocalname(p, i + 1, 0);
        if (name) printf("%s", name);
        else printf("P%d", i);
    }
    if (isvararg(p)) {
        if (p->numparams > 0) printf(", ");
        printf("...");
    }
    printf(")\n");

    char buffer[4096];
    AlccInstruction dec;
    bool pending_elseif = false;

    for (int i=0; i<p->sizecode; i++) {
        if (plugin && plugin->on_decompile_inst) {
             if (plugin->on_decompile_inst(p, i, buffer, sizeof(buffer))) {
                 printf("%*s%s\n", level*2, "", buffer);
                 continue;
             }
        }

        int lbl = get_label_id(&ja, i);
        int lbl_type = get_label_type(&ja, i);

        // Check for REPEAT start
        if (lbl_type == TARGET_REPEAT) {
             printf("%*srepeat\n", indent*2, "");
             bs_push(&bs, -1, BLOCK_REPEAT, i); // -1 target as it ends conditionally
             indent++;
        }

        if (lbl >= 0) {
            // Even if repeat, we print label for goto safety
            printf("%*s::L%d::\n", (indent-1)*2, "", lbl);
        }

        current_backend->decode_instruction((uint32_t)p->code[i], &dec);

        int status;
        while ((status = bs_check_end(&bs, i, p))) {
            if (status == 2) {
                // Check for ELSEIF
                int target = -1;
                // We check if the current instruction 'i' starts an IF.
                // We relaxed the check for target matching because nested else/ifs can shift the end target.
                if (is_conditional_jump(p, i, &target)) {
                     printf("%*selseif ", (indent-1)*2, "");
                     pending_elseif = true;
                     // Do NOT print 'else' or change indent yet
                } else {
                     printf("%*selse\n", (indent-1)*2, "");
                }
            } else {
                indent--;
                printf("%*send\n", indent*2, "");
            }
        }

        if (dec.op == OP_FORLOOP || dec.op == OP_TFORLOOP) indent--;

        if (!pending_elseif) {
             printf("%*s  ", indent*2, "");
        } else {
             // We are continuing an elseif line, no indentation needed
        }

        int op = dec.op;
        int a = dec.a;
        int b = dec.b;
        int c = dec.c;
        int k = dec.k;
        int bx = dec.bx;

        switch(op) {
            case OP_MOVE:
                print_var(p, a, i);
                if (is_toclose(p, i, a)) printf(" <toclose>");
                printf(" = "); print_var(p, b, i);
                break;
            case OP_LOADI:
            case OP_LOADF:
                print_var(p, a, i);
                if (is_toclose(p, i, a)) printf(" <toclose>");
                printf(" = %d", bx);
                break;
            case OP_LOADK:
                print_var(p, a, i);
                if (is_toclose(p, i, a)) printf(" <toclose>");
                printf(" = "); print_const(p, bx);
                break;
            case OP_VARARG:
                print_var(p, a, i);
                if (is_toclose(p, i, a)) printf(" <toclose>");
                if (c > 1) {
                    for (int j=1; j<c; j++) {
                        printf(", "); print_var(p, a+j, i);
                    }
                    printf(" = ...");
                } else if (c == 0) {
                    printf("... = ...");
                } else if (c == 1) {
                    printf(" -- vararg (0 results)");
                } else {
                    printf(" = ... (%d results)", c - 1);
                }
                break;
             case OP_VARARGPREP:
                printf("-- varargprep");
                break;
            case OP_CLOSURE: {
                Proto* sub = p->p[bx];
                char name_buf[256];
                int has_name = 0;
                int skip_next = 0;
                const char* final_name = NULL;

                if (i + 1 < p->sizecode) {
                    AlccInstruction next;
                    current_backend->decode_instruction((uint32_t)p->code[i+1], &next);

                    if (next.op == OP_SETTABUP && next.c == a) { // SETTABUP Up[A] Key[B] R[C] -> Up[A][Key] = R[C]
                         if (next.b < p->sizek && ttisstring(&p->k[next.b]) && is_identifier(getstr(tsvalue(&p->k[next.b])))) {
                             snprintf(name_buf, sizeof(name_buf), "function %s", getstr(tsvalue(&p->k[next.b])));
                             final_name = name_buf;
                             has_name = 1;
                             skip_next = 1;
                         }
                    } else if (next.op == OP_MOVE && next.b == a) { // MOVE A B. R[A] = R[B].
                        const char* loc = luaF_getlocalname(p, next.a + 1, i + 1);
                        if (loc) {
                            snprintf(name_buf, sizeof(name_buf), "local function %s", loc);
                            final_name = name_buf;
                            has_name = 1;
                            skip_next = 1;
                        }
                    } else if (next.op == OP_SETFIELD && next.c == a && next.k == 0) {
                        // SETFIELD A B C k=0. R[A][K[B]] = R[C].
                        if (next.b < p->sizek && ttisstring(&p->k[next.b]) && is_identifier(getstr(tsvalue(&p->k[next.b])))) {
                             const char* field = getstr(tsvalue(&p->k[next.b]));
                             const char* base = luaF_getlocalname(p, next.a + 1, i + 1);
                             if (base) {
                                 snprintf(name_buf, sizeof(name_buf), "function %s.%s", base, field);
                                 final_name = name_buf;
                                 has_name = 1;
                                 skip_next = 1;
                             }
                        }
                    }
                }

                if (has_name) {
                    decompile(sub, level, plugin, final_name);
                    if (skip_next) i++;
                } else {
                    print_var(p, a, i); printf(" = ");
                    decompile(sub, level + 1, plugin, NULL);
                }
                set_proto_printed(sub);
                break;
            }
            case OP_GETUPVAL:
                print_var(p, a, i); printf(" = "); print_upval(p, b);
                break;
            case OP_SETUPVAL:
                print_upval(p, b); printf(" = "); print_var(p, a, i);
                break;
            case OP_GETTABUP:
                print_var(p, a, i); printf(" = "); print_upval(p, b);
                if (ttisstring(&p->k[c]) && is_identifier(getstr(tsvalue(&p->k[c])))) {
                    printf(".%s", getstr(tsvalue(&p->k[c])));
                } else {
                    printf("["); print_const(p, c); printf("]");
                }
                break;
            case OP_GETTABLE:
                print_var(p, a, i); printf(" = "); print_var(p, b, i); printf("["); print_var(p, c, i); printf("]");
                break;
            case OP_SETTABUP:
                print_upval(p, a);
                if (ttisstring(&p->k[b]) && is_identifier(getstr(tsvalue(&p->k[b])))) {
                    printf(".%s", getstr(tsvalue(&p->k[b])));
                } else {
                    printf("["); print_const(p, b); printf("]");
                }
                printf(" = ");
                print_var(p, c, i);
                break;
            case OP_NEWTABLE: {
                // Table Constructor Logic
                print_var(p, a, i); printf(" = {");

                int table_reg = a;
                int next_pc = i + 1;
                int items = 0;

                while (next_pc < p->sizecode) {
                    AlccInstruction next_inst;
                    current_backend->decode_instruction((uint32_t)p->code[next_pc], &next_inst);

                    if (next_inst.op == OP_EXTRAARG) { next_pc++; continue; }

                    if (next_inst.op == OP_SETFIELD && next_inst.a == table_reg) {
                        if (items > 0) printf(", ");

                        if (ttisstring(&p->k[next_inst.b]) && is_identifier(getstr(tsvalue(&p->k[next_inst.b])))) {
                            printf("%s = ", getstr(tsvalue(&p->k[next_inst.b])));
                        } else {
                            printf("[");
                            print_const(p, next_inst.b);
                            printf("] = ");
                        }

                        if (next_inst.k) print_const(p, next_inst.c);
                        else print_var(p, next_inst.c, next_pc);

                        items++;
                        next_pc++;
                    } else if (next_inst.op == OP_SETLIST && next_inst.a == table_reg) {
                         // R[A][(C-1)*FPF+i] = R[A+i]
                         // B items.
                         // Just print values from registers.
                         if (items > 0) printf(", ");
                         int num = next_inst.b;
                         if (num == 0) num = 0; // Top?

                         for (int j=1; j<=num; j++) {
                             if (j>1) printf(", ");
                             print_var(p, next_inst.a + j, next_pc);
                         }
                         if (num == 0) printf("... (TOP)");

                         items++;
                         next_pc++;
                    } else if (next_inst.op == OP_SETI && next_inst.a == table_reg) {
                        if (items > 0) printf(", ");
                        printf("[%d] = ", next_inst.b);
                        if (next_inst.k) print_const(p, next_inst.c);
                        else print_var(p, next_inst.c, next_pc);
                        items++;
                        next_pc++;
                    } else if (next_inst.op == OP_SETTABLE && next_inst.a == table_reg) {
                        if (items > 0) printf(", ");
                        printf("[");
                        print_var(p, next_inst.b, next_pc);
                        printf("] = ");
                        if (next_inst.k) print_const(p, next_inst.c);
                        else print_var(p, next_inst.c, next_pc);
                        items++;
                        next_pc++;
                    } else {
                         break;
                    }
                }

                printf("}");
                i = next_pc - 1;
                break;
            }
            case OP_SETI:
                print_var(p, a, i); printf("[%d] = ", b);
                if (k) print_const(p, c); else print_var(p, c, i);
                break;
            case OP_SETTABLE:
                print_var(p, a, i); printf("["); print_var(p, b, i); printf("] = ");
                if (k) print_const(p, c); else print_var(p, c, i);
                break;
            case OP_SETLIST:
                printf("setlist("); print_var(p, a, i);
                if (b == 0) {
                     printf(", ...)"); // TOP
                } else {
                     for (int j=1; j<=b; j++) {
                         printf(", "); print_var(p, a+j, i);
                     }
                     printf(")");
                }
                break;
            case OP_GETFIELD:
                print_var(p, a, i); printf(" = "); print_var(p, b, i);
                if (ttisstring(&p->k[c]) && is_identifier(getstr(tsvalue(&p->k[c])))) {
                    printf(".%s", getstr(tsvalue(&p->k[c])));
                } else {
                    printf("["); print_const(p, c); printf("]");
                }
                break;
            case OP_SETFIELD:
                print_var(p, a, i);
                if (ttisstring(&p->k[b]) && is_identifier(getstr(tsvalue(&p->k[b])))) {
                    printf(".%s", getstr(tsvalue(&p->k[b])));
                } else {
                    printf("["); print_const(p, b); printf("]");
                }
                printf(" = ");
                if (k) print_const(p, c);
                else print_var(p, c, i);
                break;
            case OP_SELF:
                print_var(p, a+1, i); printf(" = "); print_var(p, b, i); printf("; ");
                print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(":"); print_const(p, c);
                break;
            case OP_CALL:
                if (c==0) printf("multret = ");
                else if (c==1) {}
                else if (c==2) {
                    print_var(p, a, i);
                    if (is_toclose(p, i, a)) printf(" <toclose>");
                    printf(" = ");
                }
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
                int is_else_jump = 0;
                int is_while_back = 0;

                if (bs.top > 0) {
                     int current_target = bs.blocks[bs.top-1].target_pc;
                     // Check for ELSE jump
                     if (bs.blocks[bs.top-1].type == BLOCK_IF && i == current_target - 1) {
                         int dest = i + 1 + bx;
                         if (dest > current_target) {
                             is_else_jump = 1;
                         }
                     }
                     // Check for WHILE back jump
                     if (bs.blocks[bs.top-1].type == BLOCK_WHILE) {
                         int dest = i + 1 + bx;
                         if (dest == bs.blocks[bs.top-1].start_pc) {
                             is_while_back = 1;
                         }
                     }
                }

                if (!is_else_jump && !is_while_back) {
                    int dest = i + 1 + bx;
                    int dest_lbl = get_label_id(&ja, dest);
                    if (dest_lbl >= 0) printf("goto L%d", dest_lbl);
                    else printf("goto %d", dest);
                }
                break;
            }
            case OP_FORPREP: {
                printf("for "); print_var(p, a+3, i); printf(" = ");
                print_var(p, a, i); printf(", ");
                print_var(p, a+1, i); printf(", ");
                print_var(p, a+2, i); printf(" do");
                indent++;
                break;
            }
            case OP_FORLOOP: {
                 printf("end");
                 break;
            }
            case OP_TFORPREP: {
                 int dest = i + 1 + bx;
                 int nvars = 0;
                 if (dest < p->sizecode) {
                     AlccInstruction target_inst;
                     current_backend->decode_instruction((uint32_t)p->code[dest], &target_inst);
                     if (target_inst.op == OP_TFORCALL) {
                         nvars = target_inst.c;
                     }
                 }

                 printf("for ");
                 if (nvars > 0) {
                     for (int j=0; j<nvars; j++) {
                         if (j>0) printf(", ");
                         print_var(p, a+4+j, i);
                     }
                 } else {
                     printf("<vars>");
                 }
                 printf(" in ");
                 print_var(p, a, i); printf(", ");
                 print_var(p, a+1, i); printf(", ");
                 print_var(p, a+2, i); printf(" do");
                 indent++;
                 break;
            }
            case OP_TFORCALL: {
                break;
            }
            case OP_TFORLOOP: {
                 printf("end");
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

            case OP_BAND: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" & "); print_var(p, c, i); break;
            case OP_BOR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" | "); print_var(p, c, i); break;
            case OP_BXOR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" ~ "); print_var(p, c, i); break;
            case OP_SHL: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" << "); print_var(p, c, i); break;
            case OP_SHR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" >> "); print_var(p, c, i); break;

            case OP_UNM: print_var(p, a, i); printf(" = -"); print_var(p, b, i); break;
            case OP_BNOT: print_var(p, a, i); printf(" = ~"); print_var(p, b, i); break;
            case OP_NOT: print_var(p, a, i); printf(" = not "); print_var(p, b, i); break;
            case OP_LEN: print_var(p, a, i); printf(" = #"); print_var(p, b, i); break;

            case OP_CONCAT:
                print_var(p, a, i); printf(" = ");
                for (int j=0; j<b; j++) {
                    if (j>0) printf(" .. ");
                    print_var(p, a+j, i);
                }
                break;

            case OP_EQ:
            case OP_LT:
            case OP_LE:
            case OP_EQK:
            case OP_EQI:
            case OP_LTI:
            case OP_LEI:
            case OP_GTI:
            case OP_GEI:
            case OP_TEST:
            case OP_TESTSET: {
                if (i + 1 < p->sizecode) {
                    AlccInstruction next_dec;
                    current_backend->decode_instruction((uint32_t)p->code[i+1], &next_dec);
                    if (next_dec.op == OP_JMP) {
                        int dest = i + 1 + 1 + next_dec.bx;

                        // Check for WHILE loop (forward jump to dest, dest-1 is jump back to here)
                        // Note: 'i' is the PC of this instruction. 'dest' is target of the conditional jump (loop exit).
                        // If it's a while loop, instruction at dest-1 should be JMP to i.
                        // Or JMP to i's label if labeled.
                        int is_while = 0;
                        if (dest > i) {
                             if (dest > 0 && dest <= p->sizecode) {
                                 // Check instruction at dest-1 (back-edge)
                                 AlccInstruction back_inst;
                                 // dest is 0-based index of instruction *following* the loop block?
                                 // Yes.
                                 current_backend->decode_instruction((uint32_t)p->code[dest-1], &back_inst);
                                 if (back_inst.op == OP_JMP) {
                                     int back_dest = (dest - 1) + 1 + back_inst.bx;
                                     if (back_dest == i) { // Jumps exactly to this instruction
                                         is_while = 1;
                                     } else if (lbl >= 0 && back_dest == i) { // Jumps to label? (Same pc)
                                          is_while = 1;
                                     }
                                     // Check against labeled start?
                                     // analyze_jumps ensures labels are at correct PC.
                                 }
                             }
                        }

                        // Check for REPEAT UNTIL (backward jump)
                        int is_repeat_until = 0;
                        if (dest <= i) {
                            is_repeat_until = 1;
                        }

                        if (is_while) {
                             printf("while ");
                        } else if (is_repeat_until) {
                             // For repeat..until, we are at the end.
                             // We should check if we are in a REPEAT block.
                             if (bs.top > 0 && bs.blocks[bs.top-1].type == BLOCK_REPEAT) {
                                 indent--; // Decrease indent for 'until' line
                                 printf("%*suntil ", indent*2, "");
                             } else {
                                 printf("until (orphaned) ");
                             }
                        } else {
                             if (pending_elseif) {
                                 // No "if "
                             } else {
                                 printf("if ");
                             }
                        }

                        if (op == OP_TEST || op == OP_TESTSET) {
                           if (k) printf("not ");
                           if (op == OP_TESTSET) print_var(p, b, i);
                           else print_var(p, a, i);
                        } else {
                            print_var(p, a, i);

                            const char* op_str = "??";
                            if (op == OP_EQ || op == OP_EQK || op == OP_EQI) {
                                op_str = (k) ? "~=" : "==";
                            } else if (op == OP_LT || op == OP_LTI) {
                                op_str = (k) ? ">=" : "<";
                            } else if (op == OP_LE || op == OP_LEI) {
                                op_str = (k) ? ">" : "<=";
                            } else if (op == OP_GTI) {
                                op_str = (k) ? "<=" : ">";
                            } else if (op == OP_GEI) {
                                op_str = (k) ? "<" : ">=";
                            }

                            printf(" %s ", op_str);

                            if (op == OP_EQ || op == OP_LT || op == OP_LE) print_var(p, b, i);
                            else if (op == OP_EQK) print_const(p, b);
                            else printf("%d", b - OFFSET_sC);
                        }

                        if (is_while) {
                            printf(" do");
                            indent++;
                            bs_push(&bs, dest, BLOCK_WHILE, i);
                            pending_elseif = false;
                            i++; // skip JMP
                            break;
                        } else if (is_repeat_until) {
                            // End of repeat block
                            if (bs.top > 0 && bs.blocks[bs.top-1].type == BLOCK_REPEAT) {
                                bs.top--;
                            }
                            pending_elseif = false;
                            i++; // skip JMP
                            break;
                        } else {
                            printf(" then");
                            if (!pending_elseif) {
                                indent++;
                            }
                            pending_elseif = false;
                            bs_push(&bs, dest, BLOCK_IF, i);
                            i++; // skip JMP
                            break;
                        }
                    }
                }

                if (!pending_elseif) printf("if (conditional check failed)");
                else printf("(conditional check failed)");
                break;
            }

            case OP_ADDI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" + %d", c - OFFSET_sC); break;
            case OP_SHLI: print_var(p, a, i); printf(" = %d << ", c - OFFSET_sC); print_var(p, b, i); break;
            case OP_SHRI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" >> %d", c - OFFSET_sC); break;

            case OP_TBC:
                // Handled in assignment
                break;

            case OP_MMBIN:
            case OP_MMBINI:
            case OP_MMBINK:
                printf("-- mmbin");
                break;

            case OP_CLOSE:
                printf("close_scope("); print_var(p, a, i); printf(")");
                break;

            default: {
                const char* name = current_backend->get_op_name(op);
                if (name) printf("; %s", name);
                else printf("; OP_%d", op);
            }
        }
        printf("\n");

        // Reset pending_elseif if it wasn't used (e.g. if we encountered a non-conditional instruction inside an elseif candidate... wait that shouldn't happen if is_conditional_jump checked it)
        pending_elseif = false;
    }

    printf("%*send\n", level*2, "");

    for (int i=0; i<p->sizep; i++) {
        if (!is_proto_printed(p->p[i])) {
            decompile(p->p[i], level+1, plugin);
        }
    }
}
