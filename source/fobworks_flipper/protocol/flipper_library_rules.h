#pragma once

#include <stdbool.h>

#define FLIPPER_LIBRARY_NAME_LIMIT 48
#define FLIPPER_LIBRARY_PAGE_LIMIT 16
#define FLIPPER_LIBRARY_PULSE_LIMIT 512

/* Pure, transport-independent validation used by both the SD library and
   host contract tests.  These helpers do not open files or know a filesystem
   implementation. */
bool flipper_lib_name_is_safe(const char* name);
int flipper_lib_page_offset(int offset);
int flipper_lib_page_limit(int limit);
bool flipper_lib_pulse_count_is_safe(int count);
/* Stable cross-boot eviction ordering; deliberately lexical, not chronological. */
bool flipper_lib_eviction_name_before(const char* candidate, const char* victim);