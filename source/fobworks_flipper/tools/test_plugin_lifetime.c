#include "../protocol/flipper_plugin.h"
#include <flipper_application/flipper_application.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

atomic_int test_frees;
FlipperApplicationPreloadStatus test_preload_status;
FlipperApplicationLoadStatus test_load_status;
const FlipperAppPluginDescriptor* test_descriptor;
bool test_alloc_failure;
const FlipperAppPluginDescriptor* fw_catalog_ep(void);
static atomic_bool started;
static atomic_bool finished;

static void* unload(void* ctx) {
    (void)ctx;
    atomic_store(&started, true);
    flipper_plugin_unload_all();
    atomic_store(&finished, true);
    return NULL;
}

int main(void) {
    flipper_plugin_init();
    test_alloc_failure = true;
    assert(!flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: memory") == 0);
    test_alloc_failure = false;
    test_preload_status = FlipperApplicationPreloadStatusInvalidFile;
    assert(!flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: file/API") == 0);
    test_preload_status = FlipperApplicationPreloadStatusNotEnoughMemory;
    assert(!flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: memory") == 0);
    const char* labels[32] = {0};
    assert(flipper_catalog_makes(labels, 32) == 0);
    assert(labels[0] == NULL);
    assert(strcmp(flipper_catalog_error(), "Catalog: memory") == 0);
    test_preload_status = FlipperApplicationPreloadStatusSuccess;
    test_load_status = FlipperApplicationLoadStatusMissingImports;
    assert(!flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: imports") == 0);
    test_load_status = FlipperApplicationLoadStatusUnspecifiedError;
    assert(!flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: mapping") == 0);
    test_load_status = FlipperApplicationLoadStatusSuccess;
    assert(!flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: ABI") == 0);
    static const FobworksPluginHeader old_header = {
        .kind = FobworksPluginKindCatalog, .abi = FOBWORKS_PLUGIN_ABI - 1};
    static const FlipperAppPluginDescriptor old_descriptor = {.entry_point = &old_header};
    test_descriptor = &old_descriptor;
    assert(!flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: ABI") == 0);
    test_descriptor = fw_catalog_ep();
    assert(flipper_catalog_ensure());
    assert(strcmp(flipper_catalog_error(), "Catalog: empty") == 0);
    const FobworksCatalogApi* catalog = flipper_catalog_api();
    static const char* const expected_makes[] = {
        "Hyundai", "Kia", "Nissan", "Toyota", "Lexus", "Ford / Lincoln (NA)",
        "Ford EU", "Mazda", "Honda", "Chrysler/Dodge/Jeep/RAM",
        "GM (Chevy/GMC/Buick/Cadillac)", "Subaru (NA)", "Genesis",
        "VW/Audi/Skoda/SEAT (EU)", "Volkswagen (NA)", "BMW / Mercedes (EU)",
        "Peugeot / Citroen (EU)", "Renault (EU)", "Opel / Vauxhall (EU)",
        "Fiat / Alfa Romeo (EU)", "Volvo (EU)", "Honda EU", "Nissan EU",
        "Subaru EU", "Mazda EU", "EU Gate & Garage", "LiftMaster", "CAME",
        "Nice", "Generic",
    };
    assert(flipper_catalog_makes(labels, 32) == 30);
    for(int i = 0; i < 30; i++) assert(strcmp(labels[i], expected_makes[i]) == 0);
    assert(labels[30] == NULL);
    const char* bounded[3] = {NULL, NULL, "guard"};
    assert(flipper_catalog_makes(bounded, 2) == 2);
    assert(strcmp(bounded[0], "Hyundai") == 0 && strcmp(bounded[1], "Kia") == 0);
    assert(strcmp(bounded[2], "guard") == 0);
    assert(flipper_catalog_makes(NULL, 32) == 0);
    assert(flipper_catalog_makes(labels, 0) == 0);
    assert(flipper_catalog_makes(labels, -1) == 0);
    for(int run = 0; run < 3; run++) {
        flipper_plugin_unload_all();
        assert(flipper_catalog_makes(labels, 32) == 30);
        for(int i = 0; i < 30; i++) assert(strcmp(labels[i], expected_makes[i]) == 0);
    }
    catalog = flipper_catalog_api();
    puts("Shared Make picker: 30 ordered makes, bounds, failure and reload: PASS");
    assert(catalog->fc_vehicle_count() == FLIPPER_FC_VEHICLE_COUNT);
    assert(catalog->fbk_make_count() == FLIPPER_FBK_MAKE_COUNT);
    assert(catalog->fc_vehicle_count() > 0 && catalog->fbk_make_count() > 0);
    for(int i = 0; i < catalog->fc_vehicle_count(); i++) {
        const FlipperFcVehicle* v = catalog->fc_vehicle_at(i);
        assert(v && v->make && v->make[0] && v->model && v->model[0]);
        assert(v->years);
        assert(v->year_count > 0 && v->year_count <= FC_YEARS_MAX);
        for(int y = 0; y < v->year_count; y++) assert(v->years[y] && v->years[y][0]);
    }
    for(int i = 0; i < catalog->fbk_make_count(); i++) {
        const FlipperFbkMake* make = catalog->fbk_make_at(i);
        assert(make && make->make && make->make[0]);
        assert(make->models);
        assert(make->model_count > 0 && make->model_count <= FBK_MODELS_MAX);
        for(int j = 0; j < make->model_count; j++) {
            assert(catalog->fbk_model_name(i, j));
            assert(catalog->fbk_model_profile(i, j));
            assert(strcmp(make->models[j].model, catalog->fbk_model_name(i, j)) == 0);
        }
    }
    assert(!catalog->fc_vehicle_at(-1));
    assert(!catalog->fc_vehicle_at(catalog->fc_vehicle_count()));
    assert(!catalog->fbk_make_at(-1));
    assert(!catalog->fbk_make_at(catalog->fbk_make_count()));
    printf("Catalog plugin: %d vehicles, %d FOBback makes; model/year bounds: PASS\n",
           catalog->fc_vehicle_count(), catalog->fbk_make_count());
    flipper_plugin_unload_all();
    test_descriptor = NULL;
    atomic_store(&test_frees, 0);
    flipper_plugin_lock();
    /* I ensure recursive API lookup does not deadlock while holding the lifetime guard. */
    assert(flipper_force_api() != NULL);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, unload, NULL) == 0);
    struct timespec pause = {0, 1000000};
    while(!atomic_load(&started)) nanosleep(&pause, NULL);
    struct timespec wait = {0, 30000000};
    nanosleep(&wait, NULL);
    assert(!atomic_load(&finished) && atomic_load(&test_frees) == 0);
    flipper_plugin_unlock();
    assert(pthread_join(thread, NULL) == 0);
    assert(atomic_load(&finished) && atomic_load(&test_frees) == 1);
    flipper_plugin_deinit();
    /* I start the next launch clean. */
    flipper_plugin_init();
    flipper_plugin_deinit();
    puts("Production plugin failures, retry and concurrent unload: PASS");
}
