#include "flipper_library_rules.h"
#include <string.h>

bool flipper_lib_name_is_safe(const char* name) {
    size_t len;
    if(!name) return false;
    len = strlen(name);
    if(len == 0 || len >= FLIPPER_LIBRARY_NAME_LIMIT) return false;
    for(size_t i = 0; i < len; i++) {
        char c = name[i];
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

int flipper_lib_page_offset(int offset) {
    if(offset < 0) return 0;
    return offset > FLIPPER_LIBRARY_PAGE_LIMIT ? FLIPPER_LIBRARY_PAGE_LIMIT : offset;
}

int flipper_lib_page_limit(int limit) {
    if(limit <= 0) return FLIPPER_LIBRARY_PAGE_LIMIT;
    return limit > FLIPPER_LIBRARY_PAGE_LIMIT ? FLIPPER_LIBRARY_PAGE_LIMIT : limit;
}

bool flipper_lib_pulse_count_is_safe(int count) {
    return count >= 0 && count <= FLIPPER_LIBRARY_PULSE_LIMIT;
}

bool flipper_lib_eviction_name_before(
    const char* candidate, const char* victim) {
    return candidate && victim && strcmp(candidate, victim) < 0;
}