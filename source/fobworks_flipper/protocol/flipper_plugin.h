#pragma once

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
const FobworksCatalogApi* flipper_catalog_api(void);

/* Map fw_force.fal (Scher-Khan and later force-only extras). */
bool flipper_force_ensure(void);
const FobworksForceApi* flipper_force_api(void);
