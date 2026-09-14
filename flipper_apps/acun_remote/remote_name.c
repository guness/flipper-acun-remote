#include "remote_name.h"
#include <string.h>

static bool allowed(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == ' ' || ch == '_' || ch == '-';
}

bool remote_name_valid(const char* name) {
    if(!name) return false;
    size_t n = strlen(name);
    if(n < 1 || n > REMOTE_NAME_MAX) return false;
    if(name[0] == ' ' || name[n - 1] == ' ') return false;
    for(size_t i = 0; i < n; ++i)
        if(!allowed(name[i])) return false;
    return true;
}
