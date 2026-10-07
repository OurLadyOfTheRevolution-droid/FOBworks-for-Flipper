#include "flipper_plugin.h"

#include <flipper_application/flipper_application.h>
#include <loader/firmware_api/firmware_api.h>
#include <storage/storage.h>
#include <furi.h>

typedef struct {
    FlipperApplication* app;
    const FobworksPluginHeader* header;
} LoadedPlugin;

static LoadedPlugin s_loaded[FlipperPluginCount];
static FuriMutex* s_lifetime;

void flipper_plugin_init(void) {
    furi_check(!s_lifetime);
    s_lifetime = furi_mutex_alloc(FuriMutexTypeRecursive);
    furi_check(s_lifetime);
}

void flipper_plugin_lock(void) {
    furi_check(s_lifetime);
    furi_mutex_acquire(s_lifetime, FuriWaitForever);
}

void flipper_plugin_unlock(void) {
    furi_mutex_release(s_lifetime);
}

void flipper_plugin_deinit(void) {
    flipper_plugin_unload_all();
    furi_mutex_free(s_lifetime);
    s_lifetime = NULL;
}

static const char* const s_paths[FlipperPluginCount] = {
    APP_ASSETS_PATH("plugins/fw_catalog.fal"),
    APP_ASSETS_PATH("plugins/fw_force.fal"),
};

static const FobworksPluginKind s_kinds[FlipperPluginCount] = {
    FobworksPluginKindCatalog,
    FobworksPluginKindForce,
};

static bool plugin_load_locked(FlipperPluginId id) {
    if(id >= FlipperPluginCount) return false;
    if(s_loaded[id].header) return true;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperApplication* app = flipper_application_alloc(storage, firmware_api_interface);
    bool ok = false;
    const FlipperAppPluginDescriptor* desc;
    const FobworksPluginHeader* hdr;

    if(flipper_application_preload(app, s_paths[id]) == FlipperApplicationPreloadStatusSuccess &&
       flipper_application_map_to_memory(app) == FlipperApplicationLoadStatusSuccess) {
        desc = flipper_application_plugin_get_descriptor(app);
        hdr = desc ? desc->entry_point : NULL;
        if(hdr && hdr->abi == FOBWORKS_PLUGIN_ABI && hdr->kind == s_kinds[id]) {
            s_loaded[id].app = app;
            s_loaded[id].header = hdr;
            app = NULL;
            ok = true;
        }
    }
    if(app) flipper_application_free(app);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static bool plugin_load(FlipperPluginId id) {
    flipper_plugin_lock();
    bool ok = plugin_load_locked(id);
    flipper_plugin_unlock();
    return ok;
}

void flipper_plugin_unload_all(void) {
    flipper_plugin_lock();
    for(int i = 0; i < FlipperPluginCount; i++) {
        if(s_loaded[i].app) {
            flipper_application_free(s_loaded[i].app);
            s_loaded[i].app = NULL;
            s_loaded[i].header = NULL;
        }
    }
    flipper_plugin_unlock();
}

void flipper_plugin_unload(FlipperPluginId id) {
    if(id >= FlipperPluginCount) return;
    flipper_plugin_lock();
    if(s_loaded[id].app) {
        flipper_application_free(s_loaded[id].app);
        s_loaded[id].app = NULL;
        s_loaded[id].header = NULL;
    }
    flipper_plugin_unlock();
}

bool flipper_catalog_ensure(void) {
    return plugin_load(FlipperPluginCatalog);
}

const FobworksCatalogApi* flipper_catalog_api(void) {
    return plugin_load(FlipperPluginCatalog) ?
               (const FobworksCatalogApi*)s_loaded[FlipperPluginCatalog].header :
               NULL;
}

bool flipper_force_ensure(void) {
    return plugin_load(FlipperPluginForce);
}

const FobworksForceApi* flipper_force_api(void) {
    return plugin_load(FlipperPluginForce) ?
               (const FobworksForceApi*)s_loaded[FlipperPluginForce].header :
               NULL;
}
