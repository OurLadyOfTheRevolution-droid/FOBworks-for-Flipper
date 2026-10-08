#pragma once
/* I initialize before workers, and deinitialize after all workers are joined.
   I hold the recursive lifetime lock while executing a borrowed plugin API. */
void flipper_plugin_init(void);
void flipper_plugin_deinit(void);
void flipper_plugin_lock(void);
void flipper_plugin_unlock(void);

#include <stdbool.h>
#include "../plugins/fobworks_plugin_api.h"

typedef enum {
    FlipperPluginCatalog = 0,
    FlipperPluginForce,
    FlipperPluginCount,
} FlipperPluginId;

void flipper_plugin_unload(FlipperPluginId id);
void flipper_plugin_unload_all(void);

/* Map fw_catalog.fal. False if the FAL is missing or the ABI does not match. */
bool flipper_catalog_ensure(void);
const char* flipper_catalog_error(void);
const FobworksCatalogApi* flipper_catalog_api(void);
/* I return ordered, unique make labels borrowed until the catalog is unloaded. */
int flipper_catalog_makes(const char** out, int capacity);

/* Map fw_force.fal (Scher-Khan and later force-only extras). */
bool flipper_force_ensure(void);
const FobworksForceApi* flipper_force_api(void);
