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
#include "lstring.h"
#include "alcc_utils.h"
#include "alcc_backend.h"
#include "../plugin/alcc_plugin.h"

#define MAX_LABELS 1000

static AlccPlugin* current_plugin = NULL;
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

typedef struct {
    int targets[MAX_LABELS];
    int count;
} JumpAnalysis;

// Block Stack for structured control flow
typedef struct {
    int target_pc; // PC where the block ends
    int type;      // 0: IF, 1: LOOP (handled by indent but maybe needed for break)
} Block;

typedef struct {
    Block blocks[100];
    int top;
} BlockStack;

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
        // If the instruction before this block end was a JMP to a further location,
        // it means we finished the 'THEN' block and are jumping over the 'ELSE' block.
        // So this location is the start of the 'ELSE' block.
        if (pc > 0) {
             Instruction prev = p->code[pc-1];
             if (GET_OPCODE(prev) == OP_JMP) {
                 int sJ = GETARG_sJ(prev);
                 // In Lua 5.4, JMP uses sJ (25 bits).
                 // target = pc-1 + 1 + sJ = pc + sJ.
                 // Wait, JMP is sJ. Offset is relative to pc+1.
                 int target = pc + sJ;

                 // If this JMP target is > pc, it's an else block.
                 if (target > pc) {
                     // We found an else block!
                     // Update the current block target to the new target.
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

static const char* resolve_var_name(Proto* p, int reg, int pc) {
    return luaF_getlocalname(p, reg + 1, pc);
}

static void print_var(Proto* p, int reg, int pc) {
    const char* name = resolve_var_name(p, reg, pc);
    if (name) printf("%s", name);
    else printf("R[%d]", reg);
}

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
    JumpAnalysis ja;
    analyze_jumps(p, &ja);

    BlockStack bs;
    bs.top = 0;

    // Indentation level for body
    int indent = level + 1;

    printf("%*sfunction func_%p(", level*2, "", p);
    for (int i=0; i<p->numparams; i++) {
        if (i>0) printf(", ");
        printf("P%d", i);
    }
    if (isvararg(p)) {
        if (p->numparams > 0) printf(", ");
        printf("...");
    }
    printf(")\n");

    char buffer[4096];
    AlccInstruction dec;

    for (int i=0; i<p->sizecode; i++) {
        if (current_plugin && current_plugin->on_decompile_inst) {
             if (current_plugin->on_decompile_inst(p, i, buffer, sizeof(buffer))) {
                 printf("%*s%s\n", level*2, "", buffer);
                 continue;
             }
        }

        int lbl = get_label_id(&ja, i);
        if (lbl >= 0) {
            printf("%*s::L%d::\n", (indent-1)*2, "", lbl);
        }

        current_backend->decode_instruction((uint32_t)p->code[i], &dec);

        // Check for end of blocks (IF/ELSE)
        int status;
        while ((status = bs_check_end(&bs, i, p))) {
            if (status == 2) {
                printf("%*selse\n", (indent-1)*2, "");
                // Don't indent-- because we stay in the block (just switched branches)
            } else {
                indent--;
                printf("%*send\n", indent*2, "");
            }
        }

        // Adjust indent for end of loops
        if (dec.op == OP_FORLOOP || dec.op == OP_TFORLOOP) indent--;

        // If instruction is JMP and it was part of an ELSE transition, suppress it?
        // Wait, if it's an ELSE transition, it's at i-1.
        // We are at i. JMP at i-1 was processed in previous iteration.
        // We just printed 'else'.
        // So the JMP instruction itself was printed as 'goto ...' in previous iteration?
        // Yes.
        // We want to suppress 'goto' if it's an else-jump.
        // To do that, we need to check inside OP_JMP case.

        // Skip printing indentation if we suppressed the instruction (e.g. EXTRAARG or suppressed JMP)
        // But here we print indent unconditionally.
        // Let's print indent inside switch or check op first.
        // For simplicity, print indent here, but JMP case might print nothing.
        // Actually, if JMP prints nothing, we have dangling indentation.
        // But JMP is usually on its own line.

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
                // Inline decompile
                // We need to suppress the header "function func_..." and "end" and handle indent.
                // But decompile() prints header.
                // Let's refactor decompile to accept a flag?
                // Or just print "function" here and call a body_decompile function.
                // For now, let's just call decompile recursively and fix formatting later or accept standard output.
                // Better:
                decompile(sub, level + 1);
                set_proto_printed(sub);
                break;
            }
            case OP_GETUPVAL:
                print_var(p, a, i); printf(" = U[%d]", b); // TODO: Resolve upvalue name from p->upvalues[b].name
                break;
            case OP_SETUPVAL:
                printf("U[%d] = ", b); print_var(p, a, i);
                break;
            case OP_GETTABUP:
                print_var(p, a, i); printf(" = U[%d][", b); print_const(p, c); printf("]");
                break;
            case OP_GETTABLE:
                print_var(p, a, i); printf(" = "); print_var(p, b, i); printf("["); print_var(p, c, i); printf("]");
                break;
            case OP_SETTABUP:
                printf("U[%d][", a); print_const(p, b); printf("] = ");
                // Need to know if C is register or constant/immediate.
                // In Lua 5.4+, some SETTABUP take RK or specialized.
                // Assuming R for now based on backend decode which puts generic args.
                // But wait, in 5.4, SETTABUP A B C: UpValue[A][K[B]] = RK(C)
                // If backend maps C correctly...
                printf("R[%d]", c);
                break;
            case OP_NEWTABLE:
                print_var(p, a, i); printf(" = {}");
                break;
            case OP_SETLIST:
                // vC is array start index? In 5.4: R[A][(C-1)*FPF+i] := R[A+i], 1 <= i <= B
                // Decoded: a=A, b=vB, c=vC, k=k.
                // If b==0, use TOP.
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
                // R[A+1] := R[B]; R[A] := R[B][K[C]:shortstring]
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
                // Check if this is an ELSE jump
                // It is an ELSE jump if it is the last instruction of the current block (i == target - 1)
                // and it jumps forward beyond the current block target.
                int is_else_jump = 0;
                if (bs.top > 0) {
                     int current_target = bs.blocks[bs.top-1].target_pc;
                     if (i == current_target - 1) {
                         int dest = i + 1 + bx; // JMP uses bx (sJ in 5.4, but here decoded as bx/sJ)
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
                int dest = i + 1 + bx + 1;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("for "); print_var(p, a+3, i); printf(" = ... do -- jump to L%d", dest_lbl);
                 indent++;
                 break;
            }
            case OP_FORLOOP: {
                 int dest = i + 1 - bx;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("end -- forloop jump to L%d", dest_lbl);
                 break;
            }
            case OP_TFORPREP: {
                 int dest = i + 1 + bx;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("for <gen> in ... do -- jump to L%d", dest_lbl);
                 indent++;
                 break;
            }
            case OP_TFORLOOP: {
                 int dest = i + 1 - bx;
                 int dest_lbl = get_label_id(&ja, dest);
                 printf("end -- tforloop jump to L%d", dest_lbl);
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

            // Bitwise
            case OP_BAND: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" & "); print_var(p, c, i); break;
            case OP_BOR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" | "); print_var(p, c, i); break;
            case OP_BXOR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" ~ "); print_var(p, c, i); break;
            case OP_SHL: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" << "); print_var(p, c, i); break;
            case OP_SHR: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" >> "); print_var(p, c, i); break;

            // Unary
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

            // Comparison -> If
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
                // Peek next instruction for JMP
                if (i + 1 < p->sizecode) {
                    AlccInstruction next_dec;
                    current_backend->decode_instruction((uint32_t)p->code[i+1], &next_dec);
                    if (next_dec.op == OP_JMP) {
                        int dest = i + 1 + 1 + next_dec.bx; // pc+1 (next) + 1 + offset

                        // Print condition
                        printf("if (");
                        if (op == OP_TEST || op == OP_TESTSET) {
                           if (op == OP_TESTSET) {
                               print_var(p, a, i); printf(" := "); // assignment side effect
                           }
                           // condition is implicitly on A (or B for TESTSET?)
                           // OP_TESTSET A B k: if (not R[B] == k) then pc++ else R[A] := R[B]
                           // Wait, if condition met (skip), no assignment?
                           // Actually the instruction sets R[A] only if NOT skipping?
                           // No, usually TESTSET is `A = B` if B is true/false.
                           // `if (R[B]) R[A] = R[B]`.
                           // For decompiler, simplified:
                           if (op == OP_TESTSET) print_var(p, b, i);
                           else print_var(p, a, i);
                        } else {
                            print_var(p, a, i);

                            const char* op_str = "??";
                            if (op == OP_EQ || op == OP_EQK || op == OP_EQI) op_str = "==";
                            else if (op == OP_LT || op == OP_LTI) op_str = "<";
                            else if (op == OP_LE || op == OP_LEI) op_str = "<=";
                            else if (op == OP_GTI) op_str = ">";
                            else if (op == OP_GEI) op_str = ">=";

                            printf(" %s ", op_str);

                            if (op == OP_EQ || op == OP_LT || op == OP_LE) print_var(p, b, i);
                            else if (op == OP_EQK) print_const(p, b); // b is Bx or B? iABC so B.
                            else if (op == OP_EQI || op == OP_LTI || op == OP_LEI || op == OP_GTI || op == OP_GEI) printf("%d", b - OFFSET_sC);
                        }
                        printf(")");

                        // k handling
                        // if ((cond) ~= k) skip (true block)
                        // so if k=0 (false), skip if (cond != 0) -> if (cond)
                        // so if k=1 (true), skip if (cond != 1) -> if (not cond) (assuming bool)

                        // For TEST: if (not R[A] == k) skip.
                        // k=0: if (not R[A] == 0) -> if (R[A]). Skip if true.
                        // k=1: if (not R[A] == 1) -> if (not R[A]). Skip if false.

                        // So generally:
                        // k=0 => "then" (positive check)
                        // k=1 => "not ... then" (negative check, or inverted logic)

                        // Wait, for OP_EQ, k=1 means we skip if EQUAL.
                        // "if (a==b) then".
                        // Wait. `if ((a==b) ~= k)`.
                        // If k=1. `(a==b) ~= 1`.
                        // If a==b is true (1). `1 ~= 1` is False. Don't skip. JMP to else.
                        // So if k=1, we skip on FALSE?
                        // No.
                        // Let's re-read: `OP_EQ A B k`: `if ((R[A] == R[B]) ~= k) then pc++`.
                        // If k=1.
                        // If R[A] == R[B]. Then (true ~= 1) is (1 ~= 1) is False. No skip.
                        // So if Equal, we jump to Else.
                        // If Not Equal. Then (false ~= 1) is (0 ~= 1) is True. Skip.
                        // So k=1 means "Skip if Not Equal".
                        // So "Then" block is executed if Not Equal.
                        // So `if (a ~= b) then`.

                        // If k=0.
                        // If R[A] == R[B]. Then (true ~= 0) is (1 ~= 0) is True. Skip.
                        // So "Then" block is executed if Equal.
                        // So `if (a == b) then`.

                        // So k=0 means `if (cond)`.
                        // k=1 means `if not (cond)`.

                        if (op == OP_TEST || op == OP_TESTSET) {
                             if (k) printf(" is false then"); // k=1 -> skip if false -> if R[A] is false
                             else printf(" is true then"); // k=0 -> skip if true -> if R[A] is true
                        } else {
                             if (k) printf(" is false then"); // k=1 -> skip if false -> if (cond) is false -> if not (cond)
                             else printf(" is true then"); // k=0 -> skip if true -> if (cond) is true
                        }

                        indent++;
                        bs_push(&bs, dest, 0);
                        i++; // Skip the JMP instruction as it's part of the control flow
                        break;
                    }
                }
                // Fallback (no JMP follows?)
                printf("if (conditional check failed to find JMP)");
                break;
            }

            // Immediate
            case OP_ADDI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" + %d", c - OFFSET_sC); break;
            case OP_SHLI: print_var(p, a, i); printf(" = %d << ", c - OFFSET_sC); print_var(p, b, i); break;
            case OP_SHRI: print_var(p, a, i); printf(" = "); print_var(p, b, i); printf(" >> %d", c - OFFSET_sC); break;

            // Suppress MMBIN
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
            decompile(p->p[i], level+1);
        }
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
