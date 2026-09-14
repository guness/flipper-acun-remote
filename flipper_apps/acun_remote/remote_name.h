#pragma once
#include <stdbool.h>

#define REMOTE_NAME_MAX 12

/* 1..REMOTE_NAME_MAX characters of A-Z a-z 0-9 space '_' '-', with no
 * leading or trailing space. Names become SD directory names. */
bool remote_name_valid(const char* name);
