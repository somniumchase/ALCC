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

static void my_on_disasm_header(Proto* p) {
    printf("; [PLUGIN] Disassembling Proto at %p\n", (void*)p);
}

static void my_on_asm_line(ParseCtx* ctx, char* line) {
    (void)ctx;
    // Example: Replace "REPLACE_ME" with "MOVE" in assembly input
    // This is unsafe but demonstrates the hook
    char* s = strstr(line, "REPLACE_ME");
    if (s) {
        strncpy(s, "MOVE      ", 10);
    }
}

static AlccPlugin plugin = {
    "Sample Plugin",
    my_post_load,
    my_on_instruction,
    my_on_disasm_header,
    my_on_asm_line
};

AlccPlugin* alcc_plugin_init(void) {
    return &plugin;
}
