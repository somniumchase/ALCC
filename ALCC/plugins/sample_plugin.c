#define LUA_CORE

#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lobject.h"
#include "alcc_plugin.h"
#include "lopcodes.h"
#include "lopnames.h"

static void my_post_load(lua_State* L, Proto* p) {
    (void)L;
    printf("[SAMPLE PLUGIN] Loaded function with %d instructions\n", p->sizecode);
}

static int my_on_instruction(Proto* p, int pc, char* out_buffer, size_t buffer_size) {
    Instruction inst = p->code[pc];
    OpCode op = GET_OPCODE(inst);

    // Example: Only override OP_MOVE
    if (op == OP_MOVE) {
        snprintf(out_buffer, buffer_size, "[PLUGIN] MOVE %d -> %d", GETARG_B(inst), GETARG_A(inst));
        return 1;
    }
    return 0; // Use default for others
}

static AlccPlugin plugin = {
    "Sample Plugin",
    my_post_load,
    my_on_instruction
};

AlccPlugin* alcc_plugin_init(void) {
    return &plugin;
}
