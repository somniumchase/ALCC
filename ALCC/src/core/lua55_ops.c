#define LUA_CORE

#include "alcc_opcodes.h"
#include "lopcodes.h"
#include "lopnames.h"

// Define mapping from Lua's OpMode to AlccOpMode
static AlccOpMode map_mode(enum OpMode m) {
    switch(m) {
        case iABC: return ALCC_iABC;
        case ivABC: return ALCC_ivABC;
        case iABx: return ALCC_iABx;
        case iAsBx: return ALCC_iAsBx;
        case iAx: return ALCC_iAx;
        case isJ: return ALCC_isJ;
        default: return ALCC_iABC;
    }
}

int alcc_get_num_opcodes(void) {
    return NUM_OPCODES;
}

const AlccOpInfo* alcc_get_op_info(int op) {
    static AlccOpInfo cache[NUM_OPCODES];
    static int initialized = 0;

    if (!initialized) {
        for (int i=0; i<NUM_OPCODES; i++) {
            cache[i].name = opnames[i];
            cache[i].mode = map_mode(getOpMode(i));
            // getOpMode returns enum OpMode
            // We need to check 'k' bit in opmodes byte?
            // lopcodes.h: #define testTMode(m)	(luaP_opmodes[m] & (1 << 4)) ...
            // Wait, where is 'k' stored?
            // In Lua 5.4/5.5, 'k' is an argument in instruction, not a property of opcode itself?
            // Actually instructions like OP_EQ have 'k'.
            // The FORMAT depends on mode.
            // iABC has 'k'. ivABC has 'k'. isJ has 'k'.
            // Others don't.
            // But 'k' is just a field in the instruction word.
            // Whether it is used or printed depends on opcode semantics?
            // No, lopcodes.h format diagrams show 'k' field.
            // iABC: C B k A Op
            // So ALL iABC instructions physically have a 'k' field.
            // But is it meaningful?
            // The disassembler logic: if (GETARG_k(inst)) printf(" (k)");
            // It prints it if it's set.
            // So we don't strictly need to know if opcode *uses* k, just if the format supports it.
            // iABC, ivABC, isJ support k.
            // iABx, iAsBx, iAx do not.

            AlccOpMode m = cache[i].mode;
            cache[i].has_k = (m == ALCC_iABC || m == ALCC_ivABC || m == ALCC_isJ);
        }
        initialized = 1;
    }

    if (op < 0 || op >= NUM_OPCODES) return NULL;
    return &cache[op];
}

const char* alcc_get_op_name(int op) {
    const AlccOpInfo* info = alcc_get_op_info(op);
    return info ? info->name : NULL;
}

AlccOpMode alcc_get_op_mode(int op) {
    const AlccOpInfo* info = alcc_get_op_info(op);
    return info ? info->mode : ALCC_iABC;
}
