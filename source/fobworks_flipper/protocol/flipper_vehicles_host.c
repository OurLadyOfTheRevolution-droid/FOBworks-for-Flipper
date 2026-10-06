#include "flipper_plugin.h"
#include "flipper_vehicles.h"

/*
 * Device FAP forwards catalog lookups into fw_catalog.fal. Host tests compile
 * flipper_vehicles.c instead of this file.
 */

int flipper_fc_vehicle_count(void) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fc_vehicle_count() : 0;
}

const FlipperFcVehicle* flipper_fc_vehicle_at(int i) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fc_vehicle_at(i) : NULL;
}

int flipper_fbk_make_count(void) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fbk_make_count() : 0;
}

const FlipperFbkMake* flipper_fbk_make_at(int i) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fbk_make_at(i) : NULL;
}

const FlipperFcProfile* flipper_fc_profile_by_key(const char* key) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fc_profile_by_key(key) : NULL;
}

const FlipperFbkProfile* flipper_fbk_profile_by_key(const char* key) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fbk_profile_by_key(key) : NULL;
}

const FlipperFbkProfile* flipper_fbk_model_profile(int make_idx, int model_idx) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fbk_model_profile(make_idx, model_idx) : NULL;
}

const char* flipper_fbk_model_name(int make_idx, int model_idx) {
    const FobworksCatalogApi* a = flipper_catalog_api();
    return a ? a->fbk_model_name(make_idx, model_idx) : NULL;
}
