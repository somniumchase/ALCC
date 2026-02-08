#ifndef ALCC_UTILS_H
#define ALCC_UTILS_H

#include <stdio.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include "alcc_backend.h"

// Global backend instance
extern AlccBackend* current_backend;

// Initialize a new Lua state for tools
lua_State* alcc_newstate(void);

// Skip whitespace in a string
char* alcc_skip_space(char* s);

// Print a string with escaping for display
void alcc_print_string(const char* s, size_t len);

// Parse a quoted string from input buffer into output buffer
// Returns pointer to character after the closing quote
char* alcc_parse_string(char* s, char* buffer);

// Generic Lua writer function for lua_dump
int alcc_writer(lua_State* L, const void* p, size_t sz, void* ud);

#endif
