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
#include "alcc_backend.h"
#include "../plugin/alcc_plugin.h"
#include "templates/TemplateFactory.h"
#include "templates/DefaultTemplate.h"
#include "templates/Template2.h"

static AlccPlugin* current_plugin = NULL;

int main(int argc, char** argv) {
    // Register templates
    static DefaultTemplate default_tpl;
    static Template2 tpl2;
    TemplateFactory::instance().register_template(&default_tpl);
    TemplateFactory::instance().register_template(&tpl2);
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-t template] input.luac\n", argv[0]);
        return 1;
    }

    const char* template_name = "default";
    const char* input_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                template_name = argv[++i];
            } else {
                fprintf(stderr, "Missing argument for -t\n");
                return 1;
            }
        } else {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "No input file specified\n");
        return 1;
    }

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        return 1;
    }

    StkId o = L->top.p - 1;
    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    AlccTemplate* tmpl = TemplateFactory::instance().get_template(template_name);
    if (!tmpl) {
        fprintf(stderr, "Unknown template: %s\n", template_name);
        return 1;
    }

    tmpl->decompile(p, 0, current_plugin);

    lua_close(L);
    return 0;
}
