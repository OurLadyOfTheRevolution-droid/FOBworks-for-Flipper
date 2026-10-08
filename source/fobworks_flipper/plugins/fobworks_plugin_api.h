#pragma once

#include "../protocol/flipper_decoders.h"
#include "../protocol/flipper_hitag2.h"
#include "../protocol/flipper_vehicles.h"

/* * Shared ABI between the EXTERNAL host FAP and FlipperAppType.PLUGIN FALs. * I bump FOBWORKS_PLUGIN_ABI when the structs below change; the loader rejects * a mismatch instead of calling into a stale vtable. * * Kind values let one PluginManager application_id host several FALs * (catalog vs force extras) without a second loader. */
#define FOBWORKS_PLUGIN_APPID "fobworks_flipper"
/* I bump when FobworksForceApi / FobworksCatalogApi layout changes. */
#define FOBWORKS_PLUGIN_ABI   ((uint32_t)3)

typedef enum {
    FobworksPluginKindCatalog = 1,
    FobworksPluginKindForce = 2,
} FobworksPluginKind;

typedef struct {
    FobworksPluginKind kind;
    uint32_t abi;
} FobworksPluginHeader;

typedef struct {
    FobworksPluginHeader header;
    int (*fc_vehicle_count)(void);
    const FlipperFcVehicle* (*fc_vehicle_at)(int i);
    int (*fbk_make_count)(void);
    const FlipperFbkMake* (*fbk_make_at)(int i);
    const FlipperFcProfile* (*fc_profile_by_key)(const char* key);
    const FlipperFbkProfile* (*fbk_profile_by_key)(const char* key);
    const FlipperFbkProfile* (*fbk_model_profile)(int make_idx, int model_idx);
    const char* (*fbk_model_name)(int make_idx, int model_idx);
} FobworksCatalogApi;

typedef struct {
    FobworksPluginHeader header;
    bool (*decode)(
        const FlipperPulseBuf* buf,
        FlipperDecodeResult* r,
        FlipperForceProto force);
    /* Hitag2 cipher helpers (Fiat/Renault). I parked them in the force FAL so the host image stays under the loader .text cap. Not an Auto decoder. */
    uint32_t (*hitag2_authenticate)(uint64_t key, uint32_t uid, uint32_t challenge);
    int (*hitag2_known_key_count)(void);
    const Hitag2KnownKey* (*hitag2_get_known_key)(int index);
} FobworksForceApi;
