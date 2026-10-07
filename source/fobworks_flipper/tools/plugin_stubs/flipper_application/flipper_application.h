#pragma once
#include <stdatomic.h>
#include <stdlib.h>
#include <stdbool.h>
#include <storage/storage.h>
#include "plugins/fobworks_plugin_api.h"
typedef struct { int alive; } FlipperApplication;
typedef struct { const void* entry_point; } FlipperAppPluginDescriptor;
enum { FlipperApplicationPreloadStatusSuccess, FlipperApplicationLoadStatusSuccess };
extern atomic_int test_frees;
static inline FlipperApplication* flipper_application_alloc(Storage* s, const void* api) {
    (void)s; (void)api;
    return calloc(1, sizeof(FlipperApplication));
}
static inline int flipper_application_preload(FlipperApplication* a, const char* path) {
    (void)a; (void)path; return FlipperApplicationPreloadStatusSuccess;
}
static inline int flipper_application_map_to_memory(FlipperApplication* a) {
    (void)a; return FlipperApplicationLoadStatusSuccess;
}
static inline const FlipperAppPluginDescriptor*
flipper_application_plugin_get_descriptor(FlipperApplication* a) {
    (void)a;
    static const FobworksPluginHeader header = {
        .kind = FobworksPluginKindForce, .abi = FOBWORKS_PLUGIN_ABI};
    static const FlipperAppPluginDescriptor descriptor = {&header};
    return &descriptor;
}
static inline void flipper_application_free(FlipperApplication* a) {
    atomic_fetch_add(&test_frees, 1);
    free(a);
}
