#include "alcc_utils.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// Default backend
#ifdef LUA_53
extern AlccBackend alcc_lua53_backend;
AlccBackend* current_backend = &alcc_lua53_backend;
#elif defined(LUA_52)
extern AlccBackend alcc_lua52_backend;
AlccBackend* current_backend = &alcc_lua52_backend;
#else
extern AlccBackend alcc_lua55_backend;
AlccBackend* current_backend = &alcc_lua55_backend;
#endif

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
        else if (c == '\a') printf("\\a");
        else if (c == '\b') printf("\\b");
        else if (c == '\f') printf("\\f");
        else if (c == '\v') printf("\\v");
        else if (isprint(c)) printf("%c", c);
        else printf("\\x%02x", c);
    }
    printf("\"");
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char* alcc_parse_string(char* s, char* buffer) {
    s = alcc_skip_space(s);
    if (*s != '"') return NULL;
    s++;
    char* d = buffer;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            if (*s == 'a') *d++ = '\a';
            else if (*s == 'b') *d++ = '\b';
            else if (*s == 'f') *d++ = '\f';
            else if (*s == 'n') *d++ = '\n';
            else if (*s == 'r') *d++ = '\r';
            else if (*s == 't') *d++ = '\t';
            else if (*s == 'v') *d++ = '\v';
            else if (*s == '\\') *d++ = '\\';
            else if (*s == '"') *d++ = '"';
            else if (*s == '\n') *d++ = '\n'; // escaped newline
            else if (*s == 'z') { // skip whitespace
                s++;
                while (*s && isspace(*s)) s++;
                continue;
            }
            else if (*s == 'x') {
                s++;
                int h1 = hex_digit(s[0]);
                int h2 = hex_digit(s[1]);
                if (h1 >= 0 && h2 >= 0) {
                    *d++ = (char)((h1 << 4) | h2);
                    s += 2;
                    continue;
                }
            }
            else if (isdigit(*s)) {
                int val = 0;
                int c = 0;
                while (c < 3 && isdigit(*s)) {
                    val = val * 10 + (*s - '0');
                    s++;
                    c++;
                }
                if (val > 255) val = 255; // clamp?
                *d++ = (char)val;
                continue;
            }
            else {
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
