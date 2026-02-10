#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
}
#include "alcc_utils.h"
#include "core/compat.h"
#include "../plugin/alcc_plugin.h"
#include "../templates/TemplateFactory.h"
#include "../templates/DefaultTemplate.h"
#include "../templates/Template2.h"

static AlccPlugin* current_plugin = NULL;

static void load_plugin(const char* path) {
    void* handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "Error loading plugin %s: %s\n", path, dlerror());
        exit(1);
    }

    alcc_plugin_init_fn init = (alcc_plugin_init_fn)dlsym(handle, "alcc_plugin_init");
    if (!init) {
        fprintf(stderr, "Plugin %s does not export alcc_plugin_init\n", path);
        exit(1);
    }

    current_plugin = init();
    printf("; Loaded plugin: %s\n", current_plugin->name);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.luac [-p plugin.so] [-t template]\n", argv[0]);
        return 1;
    }

    const char* input_file = NULL;
    std::string template_name = "default";

    // Register templates
    // In a real plugin system this might be dynamic, but for now we register built-ins.
    // The factory singleton instance manages them.
    static DefaultTemplate default_tpl;
    static Template2 tpl2;
    TemplateFactory::instance().register_template(&default_tpl);
    TemplateFactory::instance().register_template(&tpl2);

    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            if (i+1 < argc) {
                load_plugin(argv[i+1]);
                i++;
            } else {
                fprintf(stderr, "Missing plugin path\n");
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
        } else if (strcmp(argv[i], "--list-templates") == 0) {
            printf("Available templates:\n");
            for (const auto& name : TemplateFactory::instance().get_available_templates()) {
                printf("  %s\n", name.c_str());
            }
            return 0;
        } else {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "Input file required\n");
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

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        return 1;
    }

    StkId o = ALCC_TOP(L) - 1;
    if (!ttisLclosure(s2v(o))) {
        fprintf(stderr, "Not a Lua closure\n");
        return 1;
    }

    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    if (current_plugin && current_plugin->post_load) {
        current_plugin->post_load(L, p);
    }

    tpl->disassemble(p, current_plugin);

    lua_close(L);
    return 0;
}
