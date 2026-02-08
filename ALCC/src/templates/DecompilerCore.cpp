#include "DecompilerCore.h"
#include <stdio.h>
#include <string.h>

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

// Internal structures
struct JumpAnalysis {
    int targets[MAX_LABELS];
    int count;
};

// Block Stack for structured control flow
struct Block {
    int target_pc; // PC where the block ends
    int type;      // 0: IF, 1: LOOP
};

struct BlockStack {
    Block blocks[100];
    int top;
};

// Helper functions (static to this file)

static void bs_push(BlockStack* bs, int target, int type) {
    if (bs->top < 100) {
        bs->blocks[bs->top].target_pc = target;
        bs->blocks[bs->top].type = type;
        bs->top++;
    }
}

static int bs_check_end(BlockStack* bs, int pc, Proto* p) {
    if (bs->top > 0 && bs->blocks[bs->top-1].target_pc <= pc) {
        // Check for ELSE block transition
        if (pc > 0) {
             Instruction prev = p->code[pc-1];
             if (GET_OPCODE(prev) == OP_JMP) {
                 int sJ = GETARG_sJ(prev);
                 int target = pc + sJ;

                 if (target > pc) {
                     bs->blocks[bs->top-1].target_pc = target;
                     return 2; // Signal ELSE
                 }
             }
        }
        bs->top--;
        return 1; // Signal END
    }
    return 0;
}

static void analyze_jumps(Proto* p, JumpAnalysis* ja) {
    ja->count = 0;
    AlccInstruction dec;

    for (int i=0; i<p->sizecode; i++) {
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
            int found = 0;
            for (int j=0; j<ja->count; j++) {
                if (ja->targets[j] == target) { found=1; break; }
            }
            if (!found && ja->count < MAX_LABELS) {
                ja->targets[ja->count++] = target;
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
        alcc_print_string(getstr(p->upvalues[idx].name), tsslen(p->upvalues[idx].name));
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


void DecompilerCore::decompile(Proto* p, int level, AlccPlugin* plugin) {
    JumpAnalysis ja;
    analyze_jumps(p, &ja);

    BlockStack bs;
    bs.top = 0;

    int indent = level + 1;

    printf("%*sfunction func_%p(", level*2, "", p);
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

    for (int i=0; i<p->sizecode; i++) {
        if (plugin && plugin->on_decompile_inst) {
             if (plugin->on_decompile_inst(p, i, buffer, sizeof(buffer))) {
                 printf("%*s%s\n", level*2, "", buffer);
                 continue;
             }
        }

        int lbl = get_label_id(&ja, i);
        if (lbl >= 0) {
            printf("%*s::L%d::\n", (indent-1)*2, "", lbl);
        }

        current_backend->decode_instruction((uint32_t)p->code[i], &dec);

        int status;
        while ((status = bs_check_end(&bs, i, p))) {
            if (status == 2) {
                printf("%*selse\n", (indent-1)*2, "");
            } else {
                indent--;
                printf("%*send\n", indent*2, "");
            }
        }

        if (dec.op == OP_FORLOOP || dec.op == OP_TFORLOOP) indent--;

        printf("%*s  ", indent*2, "");

        int op = dec.op;
        int a = dec.a;
        int b = dec.b;
        int c = dec.c;
        int k = dec.k;
        int bx = dec.bx;

        switch(op) {
            case OP_MOVE:
                print_var(p, a, i); printf(" = "); print_var(p, b, i);
                break;
            case OP_LOADI:
            case OP_LOADF:
                print_var(p, a, i); printf(" = %d", bx);
                break;
            case OP_LOADK:
                print_var(p, a, i); printf(" = "); print_const(p, bx);
                break;
            case OP_VARARG:
                print_var(p, a, i);
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
                print_var(p, a, i); printf(" = ");
                decompile(sub, level + 1, plugin);
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
                print_var(p, a, i); printf(" = "); print_upval(p, b); printf("["); print_const(p, c); printf("]");
                break;
            case OP_GETTABLE:
                print_var(p, a, i); printf(" = "); print_var(p, b, i); printf("["); print_var(p, c, i); printf("]");
                break;
            case OP_SETTABUP:
                print_upval(p, a); printf("["); print_const(p, b); printf("] = ");
                print_var(p, c, i);
                break;
            case OP_NEWTABLE:
                print_var(p, a, i); printf(" = {}");
                break;
            case OP_SETLIST:
                printf("-- SETLIST "); print_var(p, a, i);
                if (b > 0) printf(" (size %d)", b);
                else printf(" (size TOP)");
                break;
            case OP_GETFIELD:
                print_var(p, a, i); printf(" = "); print_var(p, b, i); printf("."); print_const(p, c);
                break;
            case OP_SETFIELD:
                print_var(p, a, i); printf("."); print_const(p, b); printf(" = ");
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
                else if (c==2) { print_var(p, a, i); printf(" = "); }
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
                if (bs.top > 0) {
                     int current_target = bs.blocks[bs.top-1].target_pc;
                     if (i == current_target - 1) {
                         int dest = i + 1 + bx;
                         if (dest > current_target) {
                             is_else_jump = 1;
                         }
                     }
                }

                if (!is_else_jump) {
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

                        printf("if ");
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
                        printf(" then");

                        indent++;
                        bs_push(&bs, dest, 0);
                        i++;
                        break;
                    }
                }
                printf("if (conditional check failed)");
                break;
            }

            case OP_ADDI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" + %d", c - OFFSET_sC); break;
            case OP_SHLI: print_var(p, a, i); printf(" = %d << ", c - OFFSET_sC); print_var(p, b, i); break;
            case OP_SHRI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" >> %d", c - OFFSET_sC); break;

            case OP_MMBIN:
            case OP_MMBINI:
            case OP_MMBINK:
                printf("-- mmbin");
                break;

            default: {
                const char* name = current_backend->get_op_name(op);
                if (name) printf("; %s", name);
                else printf("; OP_%d", op);
            }
        }
        printf("\n");
    }

    printf("%*send\n", level*2, "");

    for (int i=0; i<p->sizep; i++) {
        if (!is_proto_printed(p->p[i])) {
            decompile(p->p[i], level+1, plugin);
        }
    }
}
