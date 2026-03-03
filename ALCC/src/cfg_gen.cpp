#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
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
#include "alcc_utils.h"
#include "core/compat.h"
#include "alcc_backend.h"

#include <vector>
#include <set>
#include <map>

struct BasicBlock {
    int id;
    int start_pc;
    int end_pc;
    std::vector<int> successors;
};

// Map of start_pc -> BasicBlock*
typedef std::map<int, BasicBlock*> BlockMap;

static void analyze_cfg(Proto* p, BlockMap& blocks) {
    std::set<int> leaders;
    leaders.insert(0); // Entry point is always a leader

    // Pass 1: Identify all leaders
    AlccInstruction dec;
    for (int i = 0; i < p->sizecode; i++) {
        current_backend->decode_instruction((uint32_t)p->code[i], &dec);
        int op = dec.op;

        int target = -1;
        bool is_branch = false;
        bool is_return = false;

        if (op == OP_JMP) {
            target = i + 1 + dec.bx;
            is_branch = true;
        } else if (op == OP_FORLOOP || op == OP_TFORLOOP) {
            target = i + 1 - dec.bx;
            is_branch = true;
        } else if (op == OP_FORPREP) {
            target = i + 1 + dec.bx + 1;
            is_branch = true;
        } else if (op == OP_RETURN || op == OP_RETURN0 || op == OP_RETURN1) {
            is_return = true;
        } else if (op == OP_EQ || op == OP_LT || op == OP_LE || op == OP_EQK || op == OP_EQI ||
                   op == OP_LTI || op == OP_LEI || op == OP_GTI || op == OP_GEI ||
                   op == OP_TEST || op == OP_TESTSET) {
            is_branch = true;
            // Conditional jumps typically fall through or skip the next instruction (which is usually a JMP)
            // We just mark it as a branch to break the block. The actual target is often handled by the next JMP.
            // But to be safe, any instruction after a branch/return is a leader.
        }

        if (is_branch && target >= 0 && target < p->sizecode) {
            leaders.insert(target);
        }

        if (is_branch || is_return) {
            if (i + 1 < p->sizecode) {
                leaders.insert(i + 1);
            }
        }
    }

    // Pass 2: Create Basic Blocks
    std::vector<int> sorted_leaders(leaders.begin(), leaders.end());
    int block_id = 0;
    for (size_t k = 0; k < sorted_leaders.size(); k++) {
        BasicBlock* bb = new BasicBlock();
        bb->id = block_id++;
        bb->start_pc = sorted_leaders[k];
        bb->end_pc = (k + 1 < sorted_leaders.size()) ? sorted_leaders[k + 1] - 1 : p->sizecode - 1;
        blocks[bb->start_pc] = bb;
    }

    // Pass 3: Determine Successors
    for (auto const& [start_pc, bb] : blocks) {
        int end_pc = bb->end_pc;
        current_backend->decode_instruction((uint32_t)p->code[end_pc], &dec);
        int op = dec.op;

        int target = -1;
        bool falls_through = true;
        bool is_return = false;

        if (op == OP_JMP) {
            target = end_pc + 1 + dec.bx;
            falls_through = false; // Unconditional jump
        } else if (op == OP_FORLOOP || op == OP_TFORLOOP) {
            target = end_pc + 1 - dec.bx;
            falls_through = true; // Conditional loop
        } else if (op == OP_FORPREP) {
            target = end_pc + 1 + dec.bx + 1;
            falls_through = true; // Conditional loop start
        } else if (op == OP_RETURN || op == OP_RETURN0 || op == OP_RETURN1) {
            is_return = true;
            falls_through = false;
        } else if (op == OP_EQ || op == OP_LT || op == OP_LE || op == OP_EQK || op == OP_EQI ||
                   op == OP_LTI || op == OP_LEI || op == OP_GTI || op == OP_GEI ||
                   op == OP_TEST || op == OP_TESTSET) {
            // It's a conditional. Target is handled if the next instruction is JMP,
            // but the next instruction is the start of the next block.
            // Wait, in Lua 5.4/5.5, the JMP is indeed the next instruction.
            // Let's just fallthrough to the next block (which might be the JMP).
            falls_through = true;
        }

        if (target >= 0 && target < p->sizecode) {
            if (blocks.find(target) != blocks.end()) {
                bb->successors.push_back(target);
            }
        }

        if (falls_through && !is_return && end_pc + 1 < p->sizecode) {
            if (blocks.find(end_pc + 1) != blocks.end()) {
                bb->successors.push_back(end_pc + 1);
            }
        }
    }
}

static void print_cfg_dot(Proto* p, BlockMap& blocks) {
    printf("digraph CFG {\n");
    printf("  node [shape=box, fontname=\"Courier\"];\n");

    for (auto const& [start_pc, bb] : blocks) {
        printf("  block_%d [label=\"Block %d\\n", bb->id, bb->id);

        AlccInstruction dec;
        for (int i = bb->start_pc; i <= bb->end_pc; i++) {
            current_backend->decode_instruction((uint32_t)p->code[i], &dec);
            const AlccOpInfo* info = current_backend->get_op_info(dec.op);
            if (!info) {
                printf("[%03d] UNKNOWN\\l", i + 1);
                continue;
            }

            printf("[%03d] %-12s", i + 1, info->name);

            switch (info->mode) {
                case ALCC_iABC:
                case ALCC_ivABC:
                    printf("%d %d %d", dec.a, dec.b, dec.c);
                    if (info->has_k && dec.k) printf(" (k)");
                    break;
                case ALCC_iABx:
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
            printf("\\l");
        }
        printf("\"];\n");

        for (int succ : bb->successors) {
            if (blocks.find(succ) != blocks.end()) {
                BasicBlock* target_bb = blocks[succ];
                printf("  block_%d -> block_%d;\n", bb->id, target_bb->id);
            }
        }
    }

    printf("}\n");
}

static void generate_cfg_for_proto(Proto* p) {
    BlockMap blocks;
    analyze_cfg(p, blocks);
    print_cfg_dot(p, blocks);

    for (auto const& [start_pc, bb] : blocks) {
        delete bb;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.luac\n", argv[0]);
        return 1;
    }

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

    generate_cfg_for_proto(p);

    lua_close(L);
    return 0;
}
