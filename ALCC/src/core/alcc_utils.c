#include "alcc_utils.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

lua_State* alcc_newstate(void) {
    lua_State* L = luaL_newstate();
    if (!L) return NULL;
    lua_gc(L, LUA_GCSTOP, 0); // Stop GC initially
    return L;
}

char* alcc_skip_space(char* s) {
    while (*s && isspace(*s)) s++;
    return s;
}

void alcc_print_string(const char* s, size_t len) {
    printf("\"");
    for (size_t i=0; i<len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') printf("\\\"");
        else if (c == '\\') printf("\\\\");
        else if (c == '\n') printf("\\n");
        else if (c == '\r') printf("\\r");
        else if (c == '\t') printf("\\t");
        else if (isprint(c)) printf("%c", c);
        else printf("\\x%02x", c);
    }
    printf("\"");
}

char* alcc_parse_string(char* s, char* buffer) {
    s = alcc_skip_space(s);
    if (*s != '"') return NULL;
    s++;
    char* d = buffer;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            if (*s == 'n') *d++ = '\n';
            else if (*s == 'r') *d++ = '\r';
            else if (*s == 't') *d++ = '\t';
            else if (*s == '\\') *d++ = '\\';
            else if (*s == '"') *d++ = '"';
            else if (*s == 'x') {
                int h;
                if (sscanf(s+1, "%02x", &h) == 1) {
                    *d++ = (char)h;
                    s += 2;
                } else {
                    *d++ = 'x';
                }
            } else {
                *d++ = *s;
            }
        } else {
            *d++ = *s;
        }
        s++;
    }
    *d = '\0';
    if (*s == '"') s++;
    return s;
}

int alcc_writer(lua_State* L, const void* p, size_t sz, void* ud) {
    (void)L;
    return (fwrite(p, sz, 1, (FILE*)ud) != 1) && (sz != 0);
}
