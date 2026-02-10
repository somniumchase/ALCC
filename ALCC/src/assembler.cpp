#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lstring.h"
}
#include "alcc_utils.h"
#include "core/compat.h"
#include "../plugin/alcc_plugin.h"
#include "../templates/TemplateFactory.h"
#include "../templates/DefaultTemplate.h"
#include "../templates/Template2.h"

// Note: ParseCtx is defined in alcc_plugin.h

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.asm -o output.luac [-t template]\n", argv[0]);
        return 1;
    }

    const char* input_file = NULL;
    const char* output_file = NULL;
    std::string template_name = "default";

    // Register templates
    static DefaultTemplate default_tpl;
    static Template2 tpl2;
    TemplateFactory::instance().register_template(&default_tpl);
    TemplateFactory::instance().register_template(&tpl2);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_file = argv[i+1];
                i++;
            } else {
                fprintf(stderr, "Missing output file\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i+1 < argc) {
                template_name = argv[i+1];
                i++;
            } else {
                fprintf(stderr, "Missing template name\n");
                return 1;
            }
        } else {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "Input file required\n");
        return 1;
    }
    if (!output_file) {
        fprintf(stderr, "Output file required (-o output.luac)\n");
        return 1;
    }

    AlccTemplate* tpl = TemplateFactory::instance().get_template(template_name);
    if (!tpl) {
        fprintf(stderr, "Unknown template: %s\nAvailable templates:\n", template_name.c_str());
        for (const auto& name : TemplateFactory::instance().get_available_templates()) {
            fprintf(stderr, "  %s\n", name.c_str());
        }
        return 1;
    }

    FILE* f = fopen(input_file, "r");
    if (!f) {
        fprintf(stderr, "Cannot open input file %s\n", input_file);
        return 1;
    }

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    ParseCtx ctx;
    ctx.f = f;
    ctx.line_no = 0;

    Proto* p = tpl->assemble(L, &ctx, NULL); // No plugin support for assembler yet in main arg parsing?
    // Wait, assembler code uses plugin in `parse_proto`.
    // But `main` doesn't load plugin here.
    // The previous assembler.cpp didn't load plugin?
    // Ah, it didn't seem to support -p in main.
    // Let's check the old assembler.cpp

    fclose(f);

    if (!p) {
        fprintf(stderr, "Assembly failed\n");
        lua_close(L);
        return 1;
    }

    LClosure* cl = luaF_newLclosure(L, 1);
    cl->p = p;

    setclLvalue2s(L, ALCC_TOP(L), cl);
    ALCC_TOP(L)++;

    FILE* fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Cannot open output file %s\n", output_file);
        return 1;
    }

    if (lua_dump(L, alcc_writer, fout, 0) != 0) {
        fprintf(stderr, "Error dumping chunk\n");
    }

    fclose(fout);
    lua_close(L);
    return 0;
}
