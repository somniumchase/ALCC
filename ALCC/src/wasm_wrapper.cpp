#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

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
#include "plugin/alcc_plugin.h"
#include "templates/TemplateFactory.h"
#include "templates/DefaultTemplate.h"
#include "templates/Template2.h"

static DefaultTemplate default_tpl;
static Template2 tpl2;
static bool templates_registered = false;

void init_templates() {
    if (!templates_registered) {
        TemplateFactory::instance().register_template(&default_tpl);
        TemplateFactory::instance().register_template(&tpl2);
        templates_registered = true;
    }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int alcc_compile(const char* input_file, const char* output_file) {
    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    FILE* f = fopen(output_file, "wb");
    if (!f) {
        lua_close(L);
        return 1;
    }

    if (ALCC_LUA_DUMP(L, alcc_writer, f, 0) != 0) {
        fclose(f);
        lua_close(L);
        return 1;
    }

    fclose(f);
    lua_close(L);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int alcc_disassemble(const char* input_file, const char* template_name) {
    init_templates();

    AlccTemplate* tpl = TemplateFactory::instance().get_template(template_name);
    if (!tpl) return 1;

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    StkId o = ALCC_PEEK_TOP(L, -1);
    if (!ttisLclosure(s2v(o))) {
        lua_close(L);
        return 1;
    }

    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    tpl->disassemble(p, NULL);

    lua_close(L);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int alcc_assemble(const char* input_file, const char* output_file, const char* template_name) {
    init_templates();

    AlccTemplate* tpl = TemplateFactory::instance().get_template(template_name);
    if (!tpl) return 1;

    FILE* f = fopen(input_file, "r");
    if (!f) return 1;

    lua_State* L = alcc_newstate();
    if (!L) {
        fclose(f);
        return 1;
    }

    ParseCtx ctx;
    ctx.f = f;
    ctx.line_no = 0;

    Proto* p = tpl->assemble(L, &ctx, NULL);
    fclose(f);

    if (!p) {
        lua_close(L);
        return 1;
    }

    ALCC_LCLOSURE_T* cl = ALCC_NEW_LCLOSURE(L, 1);
    ALCC_SET_CL_PROTO(cl, p);
    ALCC_SET_TOP_LCLOSURE(L, cl);

    FILE* fout = fopen(output_file, "wb");
    if (!fout) {
        lua_close(L);
        return 1;
    }

    if (ALCC_LUA_DUMP(L, alcc_writer, fout, 0) != 0) {
        fclose(fout);
        lua_close(L);
        return 1;
    }

    fclose(fout);
    lua_close(L);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int alcc_decompile(const char* input_file, const char* template_name) {
    init_templates();

    AlccTemplate* tpl = TemplateFactory::instance().get_template(template_name);
    if (!tpl) return 1;

    lua_State* L = alcc_newstate();
    if (!L) return 1;

    if (luaL_loadfile(L, input_file) != LUA_OK) {
        fprintf(stderr, "Error loading file: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    StkId o = ALCC_TOP(L) - 1;
    LClosure* cl_obj = clLvalue(s2v(o));
    Proto* p = cl_obj->p;

    tpl->decompile(p, 0, NULL);

    lua_close(L);
    return 0;
}

} // extern "C"
