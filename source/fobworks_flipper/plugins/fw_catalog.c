#include "fobworks_plugin_api.h"

#include <flipper_application/flipper_application.h>

static int catalog_fc_vehicle_count(void) {
    return FLIPPER_FC_VEHICLE_COUNT;
}

static const FlipperFcVehicle* catalog_fc_vehicle_at(int i) {
    if(i < 0 || i >= FLIPPER_FC_VEHICLE_COUNT) return NULL;
    return &FLIPPER_FC_VEHICLES[i];
}

static int catalog_fbk_make_count(void) {
    return FLIPPER_FBK_MAKE_COUNT;
}

static const FlipperFbkMake* catalog_fbk_make_at(int i) {
    if(i < 0 || i >= FLIPPER_FBK_MAKE_COUNT) return NULL;
    return &FLIPPER_FBK_MAKES[i];
}

static const FobworksCatalogApi catalog_api = {
    .header =
        {
            .kind = FobworksPluginKindCatalog,
            .abi = FOBWORKS_PLUGIN_ABI,
        },
    .fc_vehicle_count = catalog_fc_vehicle_count,
    .fc_vehicle_at = catalog_fc_vehicle_at,
    .fbk_make_count = catalog_fbk_make_count,
    .fbk_make_at = catalog_fbk_make_at,
    .fc_profile_by_key = flipper_fc_profile_by_key,
    .fbk_profile_by_key = flipper_fbk_profile_by_key,
    .fbk_model_profile = flipper_fbk_model_profile,
    .fbk_model_name = flipper_fbk_model_name,
};

static const FlipperAppPluginDescriptor catalog_descriptor = {
    .appid = FOBWORKS_PLUGIN_APPID,
    .ep_api_version = FOBWORKS_PLUGIN_ABI,
    .entry_point = &catalog_api,
};

const FlipperAppPluginDescriptor* fw_catalog_ep(void) {
    return &catalog_descriptor;
}
