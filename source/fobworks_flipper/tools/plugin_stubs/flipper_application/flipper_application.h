#pragma once
#include <stdatomic.h>
#include <stdlib.h>
#include <stdbool.h>
#include <storage/storage.h>
#include "plugins/fobworks_plugin_api.h"
typedef struct { int alive; } FlipperApplication;
typedef struct {
    const char* appid;
    uint32_t ep_api_version;
    const void* entry_point;
} FlipperAppPluginDescriptor;
typedef enum {
    FlipperApplicationPreloadStatusSuccess,
    FlipperApplicationPreloadStatusInvalidFile,
    FlipperApplicationPreloadStatusNotEnoughMemory,
} FlipperApplicationPreloadStatus;
typedef enum {
    FlipperApplicationLoadStatusSuccess,
    FlipperApplicationLoadStatusUnspecifiedError,
    FlipperApplicationLoadStatusMissingImports,
} FlipperApplicationLoadStatus;
extern atomic_int test_frees;
extern FlipperApplicationPreloadStatus test_preload_status;
extern FlipperApplicationLoadStatus test_load_status;
extern const FlipperAppPluginDescriptor* test_descriptor;
extern bool test_alloc_failure;
static inline FlipperApplication* flipper_application_alloc(Storage* s, const void* api) {
    (void)s; (void)api;
    return test_alloc_failure ? NULL : calloc(1, sizeof(FlipperApplication));
}
static inline int flipper_application_preload(FlipperApplication* a, const char* path) {
    (void)a; (void)path; return test_preload_status;
}
static inline int flipper_application_map_to_memory(FlipperApplication* a) {
    (void)a; return test_load_status;
}
static inline const FlipperAppPluginDescriptor*
flipper_application_plugin_get_descriptor(FlipperApplication* a) {
    (void)a;
    if(test_descriptor) return test_descriptor;
    static const FobworksPluginHeader header = {
        .kind = FobworksPluginKindForce, .abi = FOBWORKS_PLUGIN_ABI};
    static const FlipperAppPluginDescriptor descriptor = {
        .appid = FOBWORKS_PLUGIN_APPID,
        .ep_api_version = FOBWORKS_PLUGIN_ABI,
        .entry_point = &header};
    return &descriptor;
}
static inline void flipper_application_free(FlipperApplication* a) {
    atomic_fetch_add(&test_frees, 1);
    free(a);
}
