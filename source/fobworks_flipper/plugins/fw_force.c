#include "fobworks_plugin_api.h"
#include "../protocol/flipper_hitag2.h"

#include <flipper_application/flipper_application.h>

static bool force_decode(
    const FlipperPulseBuf* buf, FlipperDecodeResult* r, FlipperForceProto force) {
    if(force == FlipperForceScherKhan) return flipper_decode_scher_khan(buf, r);
    if(force == FlipperForceSecplus1) return flipper_decode_secplus1(buf, r);
    if(force == FlipperForceSecplus2) return flipper_decode_secplus2(buf, r);
    return false;
}

static const FobworksForceApi force_api = {
    .header =
        {
            .kind = FobworksPluginKindForce,
            .abi = FOBWORKS_PLUGIN_ABI,
        },
    .decode = force_decode,
    .hitag2_authenticate = hitag2_authenticate,
    .hitag2_known_key_count = hitag2_known_key_count,
    .hitag2_get_known_key = hitag2_get_known_key,
};

static const FlipperAppPluginDescriptor force_descriptor = {
    .appid = FOBWORKS_PLUGIN_APPID,
    .ep_api_version = FOBWORKS_PLUGIN_ABI,
    .entry_point = &force_api,
};

const FlipperAppPluginDescriptor* fw_force_ep(void) {
    return &force_descriptor;
}
