#include "fobworks_plugin_api.h"

#include <flipper_application/flipper_application.h>

bool flipper_decode_scher_khan(const FlipperPulseBuf* buf, FlipperDecodeResult* r);

static bool force_decode(
    const FlipperPulseBuf* buf, FlipperDecodeResult* r, FlipperForceProto force) {
    if(force == FlipperForceScherKhan) return flipper_decode_scher_khan(buf, r);
    return false;
}

static const FobworksForceApi force_api = {
    .header =
        {
            .kind = FobworksPluginKindForce,
            .abi = FOBWORKS_PLUGIN_ABI,
        },
    .decode = force_decode,
};

static const FlipperAppPluginDescriptor force_descriptor = {
    .appid = FOBWORKS_PLUGIN_APPID,
    .ep_api_version = FOBWORKS_PLUGIN_ABI,
    .entry_point = &force_api,
};

const FlipperAppPluginDescriptor* fw_force_ep(void) {
    return &force_descriptor;
}
