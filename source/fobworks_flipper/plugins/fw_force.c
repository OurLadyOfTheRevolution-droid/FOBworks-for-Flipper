#include "fobworks_plugin_api.h"
#include "../protocol/flipper_hitag2.h"
#include "../protocol/flipper_vehrke.h"
#include "../protocol/flipper_psa.h"

#include <flipper_application/flipper_application.h>

static bool force_decode(
    const FlipperPulseBuf* buf, FlipperDecodeResult* r, FlipperForceProto force) {
    if(force == FlipperForceScherKhan) return flipper_decode_scher_khan(buf, r);
    if(force == FlipperForceSecplus1) return flipper_decode_secplus1(buf, r);
    if(force == FlipperForceSecplus2) return flipper_decode_secplus2(buf, r);
    if(force == FlipperForceMazda) return flipper_decode_mazda(buf, r);
    if(force == FlipperForceHonda) return flipper_decode_honda(buf, r);
    if(force == FlipperForceHondaKr5) return flipper_decode_honda_kr5(buf, r);
    if(force == FlipperForceToyota) return flipper_decode_toyota(buf, r);
    if(force == FlipperForceNissan) return flipper_decode_nissan(buf, r);
    if(force == FlipperForcePsa) return flipper_decode_psa(buf, r);
    if(force == FlipperForceCame12) return flipper_decode_came12(buf, r);
    if(force == FlipperForceNiceFlo) return flipper_decode_nice_flo(buf, r);
    if(force == FlipperForceFaacSlh) return flipper_decode_faac_slh(buf, r);
    if(force == FlipperForceDoorhan) return flipper_decode_doorhan(buf, r);
    if(force == FlipperForceAnsonic) return flipper_decode_ansonic(buf, r);
    if(force == FlipperForceLinear10) return flipper_decode_linear10(buf, r);
    if(force == FlipperForceHoltek) return flipper_decode_holtek(buf, r);
    if(force == FlipperForcePt2262) return flipper_decode_pt2262(buf, r);
    if(force == FlipperForceEv1527) return flipper_decode_ev1527(buf, r);
    if(force == FlipperForceTpms) return flipper_decode_tpms(buf, r);
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
