#pragma once
#include <stdbool.h>
#include <string.h>

#define SDK_TOKEN_CAP 64
#define SDK_TOKEN_KEY "sdk_token"

/* Same base64url form as cmake/validate_config.cmake: 32 bytes after mgst_. */
static inline bool sdk_token_valid(const char *token)
{
    if (!token || strlen(token) != 48 || strncmp(token, "mgst_", 5)) return false;
    for (int i = 5; i < 48; ++i) {
        char c = token[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return strchr("AEIMQUYcgkosw048", token[47]) != NULL;
}
