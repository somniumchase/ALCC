#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

static int writer(lua_State* L, const void* p, size_t sz, void* ud) {
    (void)L;
    return (fwrite(p, sz, 1, (FILE*)ud) != 1) && (sz != 0);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.lua -o output.luac\n", argv[0]);
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
        // Fallback or error? Let's require -o for now as per plan
        fprintf(stderr, "Output file required (-o output.luac)\n");
        return 1;
    }

    lua_State* L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "Cannot create state\n");
        return 1;
    }

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    FILE* f = fopen(output_file, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open output file %s\n", output_file);
        lua_close(L);
        return 1;
    }

    // strip=0 (keep debug info)
    if (lua_dump(L, writer, f, 0) != 0) {
        fprintf(stderr, "Error dumping chunk\n");
        fclose(f);
        lua_close(L);
        return 1;
    }

    fclose(f);
    lua_close(L);
    return 0;
}
