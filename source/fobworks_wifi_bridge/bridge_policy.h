#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Shared by the deployed bridge and its host regression test. */
static inline bool bridge_config_valid(const char* password, const char* key) {
    if(!password || !key) return false;
    size_t p = strlen(password), k = strlen(key);
    return p >= 8 && p <= 63 && k >= 16 && k <= 64 &&
           strstr(password, "CHANGE-ME") == NULL &&
           strstr(key, "CHANGE-ME") == NULL;
}

static inline bool bridge_client_authorized(
    const bool* authenticated, size_t count, size_t client) {
    return authenticated && client < count && authenticated[client];
}
