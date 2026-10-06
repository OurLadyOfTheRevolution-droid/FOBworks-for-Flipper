#include "flipper_plugin.h"

#include <flipper_application/flipper_application.h>
#include <loader/firmware_api/firmware_api.h>
#include <storage/storage.h>
#include <furi.h>
#include <string.h>

#define TAG "FobworksPlugin"

typedef struct {
    FlipperApplication* app;
    const FobworksPluginHeader* header;
} LoadedPlugin;

static LoadedPlugin s_loaded[FlipperPluginCount];

static const char* plugin_path(FlipperPluginId id) {
    switch(id) {
    case FlipperPluginCatalog:
        return APP_ASSETS_PATH("plugins/fw_catalog.fal");
    case FlipperPluginForce:
        return APP_ASSETS_PATH("plugins/fw_force.fal");
    default:
        return NULL;
    }
}

static FobworksPluginKind plugin_kind(FlipperPluginId id) {
    switch(id) {
    case FlipperPluginCatalog:
        return FobworksPluginKindCatalog;
    case FlipperPluginForce:
        return FobworksPluginKindForce;
    default:
        return (FobworksPluginKind)0;
    }
}

static bool plugin_load(FlipperPluginId id) {
    if(id >= FlipperPluginCount) return false;
    if(s_loaded[id].header) return true;

    const char* path = plugin_path(id);
    if(!path) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperApplication* app = flipper_application_alloc(storage, firmware_api_interface);
    bool ok = false;
    do {
        FlipperApplicationPreloadStatus pre = flipper_application_preload(app, path);
        if(pre != FlipperApplicationPreloadStatusSuccess) {
            FURI_LOG_E(TAG, "preload %s: %s", path, flipper_application_preload_status_to_string(pre));
            break;
        }
        if(!flipper_application_is_plugin(app)) {
            FURI_LOG_E(TAG, "%s is not a plugin", path);
            break;
        }
        FlipperApplicationLoadStatus load = flipper_application_map_to_memory(app);
        if(load != FlipperApplicationLoadStatusSuccess) {
            FURI_LOG_E(TAG, "map %s: %s", path, flipper_application_load_status_to_string(load));
            break;
        }
        const FlipperAppPluginDescriptor* desc = flipper_application_plugin_get_descriptor(app);
        if(!desc || !desc->entry_point) break;
        if(strcmp(desc->appid, FOBWORKS_PLUGIN_APPID) != 0) break;
        if(desc->ep_api_version != FOBWORKS_PLUGIN_ABI) break;
        const FobworksPluginHeader* hdr = desc->entry_point;
        if(hdr->abi != FOBWORKS_PLUGIN_ABI || hdr->kind != plugin_kind(id)) break;
        s_loaded[id].app = app;
        s_loaded[id].header = hdr;
        app = NULL;
        ok = true;
    } while(false);

    if(app) flipper_application_free(app);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

void flipper_plugin_unload(FlipperPluginId id) {
    if(id >= FlipperPluginCount) return;
    if(s_loaded[id].app) {
        flipper_application_free(s_loaded[id].app);
        s_loaded[id].app = NULL;
        s_loaded[id].header = NULL;
    }
}

void flipper_plugin_unload_all(void) {
    for(int i = 0; i < FlipperPluginCount; i++) flipper_plugin_unload((FlipperPluginId)i);
}

bool flipper_catalog_ensure(void) {
    return plugin_load(FlipperPluginCatalog);
}

const FobworksCatalogApi* flipper_catalog_api(void) {
    if(!plugin_load(FlipperPluginCatalog)) return NULL;
    return (const FobworksCatalogApi*)s_loaded[FlipperPluginCatalog].header;
}

bool flipper_force_ensure(void) {
    return plugin_load(FlipperPluginForce);
}

const FobworksForceApi* flipper_force_api(void) {
    if(!plugin_load(FlipperPluginForce)) return NULL;
    return (const FobworksForceApi*)s_loaded[FlipperPluginForce].header;
}
