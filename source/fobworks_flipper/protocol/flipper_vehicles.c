#include "flipper_vehicles.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBclone frequency presets (FC_P). Each supplies a primary frequency,
   nearby offsets, and the CC1101 modulation to use before capture. */
/* ─────────────────────────────────────────────────────────────────────────── */
const FlipperFcProfile FLIPPER_FC_PROFILES[] = {
    {
        .key = "na_315",
        .name = "North America 315 MHz",
        .freqs = { 315.00f, 314.35f, 314.65f, 315.65f },
        .freq_count = 4,
        .mod = FlipperModOOK,
        .sweep_min_khz = 300000,
        .sweep_max_khz = 320000,
    },
    {
        .key = "eu_433",
        .name = "Europe 433.92 MHz",
        .freqs = { 433.92f, 433.67f, 434.17f, 434.42f },
        .freq_count = 4,
        .mod = FlipperModOOK,
        .sweep_min_khz = 433000,
        .sweep_max_khz = 435000,
    },
    {
        .key = "eu_868",
        .name = "Europe 868 MHz",
        .freqs = { 868.00f, 867.75f, 868.30f, 868.65f },
        .freq_count = 4,
        .mod = FlipperModOOK,
        .sweep_min_khz = 867000,
        .sweep_max_khz = 870000,
    },
    {
        .key = "na_390",
        .name = "North America 390 MHz",
        .freqs = { 390.00f, 389.75f, 390.25f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .sweep_min_khz = 389000,
        .sweep_max_khz = 391000,
    },
    {
        .key = "na_418",
        .name = "North America 418 MHz",
        .freqs = { 418.00f, 417.75f, 418.25f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .sweep_min_khz = 417000,
        .sweep_max_khz = 419000,
    },
    {
        .key = "na_915",
        .name = "North America 915 MHz",
        .freqs = { 915.00f, 914.75f, 915.25f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .sweep_min_khz = 914000,
        .sweep_max_khz = 916000,
    },
    {
        .key = "na_310",
        .name = "Honda/Acura 310-313 MHz",
        .freqs = { 313.00f, 310.00f, 309.75f, 310.25f },
        .freq_count = 4,
        .mod = FlipperModOOK,
        .sweep_min_khz = 309000,
        .sweep_max_khz = 314000,
    },
    {
        .key = "fsk_433",
        .name = "Europe 433.92 MHz 2FSK",
        .freqs = { 433.92f, 433.67f, 434.17f },
        .freq_count = 3,
        .mod = FlipperMod2FSK,
        .sweep_min_khz = 433000,
        .sweep_max_khz = 435000,
    },
    {
        .key = "all",
        .name = "Full Sweep 300-928 MHz",
        .freqs = { 433.92f },
        .freq_count = 1,
        .mod = FlipperModOOK,
        .sweep_min_khz = 299500,
        .sweep_max_khz = 928000,
    },
};

const int FLIPPER_FC_PROFILE_COUNT = (int)(sizeof(FLIPPER_FC_PROFILES) / sizeof(FLIPPER_FC_PROFILES[0]));

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBclone vehicle and year presets (FC_V). */
/* ─────────────────────────────────────────────────────────────────────────── */
#define PROFILE(k) flipper_fc_profile_by_key(k)

/* The full year-by-year table is about 8.6 KB of .rodata. The host FAP does
   not compile this file; fw_catalog.fal ships every row (FLIPPER_FAP_SLIM
   unset). The slim one-row-per-make table remains only as a fallback if this
   unit is ever linked into the host image again. */
#ifndef FLIPPER_FAP_SLIM
const FlipperFcVehicle FLIPPER_FC_VEHICLES[] = {
    /* ── Hyundai / Kia ──────────────────────────────────────────────────── */
    {
        .make = "Hyundai",
        .model = "Elantra (315 MHz)",
        .years = { "2013", "2014", "2015" },
        .year_count = 3,
    },
    {
        .make = "Hyundai",
        .model = "Elantra (433 MHz)",
        .years = { "2015", "2016" },
        .year_count = 2,
    },
    {
        .make = "Hyundai",
        .model = "ix20 EU",
        .years = { "2010", "2011", "2012", "2013", "2014", "2015", "2016", "2017" },
        .year_count = 8,
    },
    {
        .make = "Kia",
        .model = "Cerato K3 (315 MHz)",
        .years = { "2012", "2013", "2014", "2015", "2016" },
        .year_count = 5,
    },
    {
        .make = "Kia",
        .model = "Cerato K3 (433 MHz)",
        .years = { "2016", "2017", "2018" },
        .year_count = 3,
    },
    /* ── Nissan ──────────────────────────────────────────────────────────── */
    {
        .make = "Nissan",
        .model = "Latio",
        .years = { "2007", "2008", "2009", "2010", "2011", "2012" },
        .year_count = 6,
    },
    {
        .make = "Nissan",
        .model = "Sylphy",
        .years = { "2012", "2013", "2014", "2015", "2016", "2017", "2018", "2019" },
        .year_count = 8,
    },
    {
        .make = "Nissan",
        .model = "Navara",
        .years = { "2010", "2011", "2012" },
        .year_count = 3,
    },
    /* ── Toyota ──────────────────────────────────────────────────────────── */
    /* Asia-specific model entries. */
    {
        .make = "Toyota",
        .model = "Rush / Wigo S (Asia, 315 MHz)",
        .years = { "2017", "2018", "2019" },
        .year_count = 3,
    },
    /* North American/global Camry, Corolla, and Prius entries use the TG
       single- versus dual-band year split. */
    {
        .make = "Toyota",
        .model = "Camry / Corolla / Prius (315 MHz)",
        .years = { "1998-2017 (single-band)", "2018+ (dual-band Ch-B)" },
        .year_count = 2,
    },
    /* Larger North American/global SUVs and trucks use the same year split. */
    {
        .make = "Toyota",
        .model = "Highlander / Sienna / 4Runner / Sequoia (315 MHz)",
        .years = { "1998-2017 (single-band)", "2018+ (dual-band Ch-B)" },
        .year_count = 2,
    },
    /* RAV4 and Tacoma switch to the later split in 2019. */
    {
        .make = "Toyota",
        .model = "RAV4 / Tacoma / Tundra (315 MHz)",
        .years = { "1998-2018 (single-band)", "2019+ (dual-band Ch-B)" },
        .year_count = 2,
    },
    /* ── Lexus ───────────────────────────────────────────────────────────── */
    {
        .make = "Lexus",
        .model = "ES / IS / GX (315 MHz)",
        .years = { "2001-2018 (single-band)", "2019+ (dual-band Ch-B)" },
        .year_count = 2,
    },
    {
        .make = "Lexus",
        .model = "RX / NX (315 MHz)",
        .years = { "1999-2018 (single-band)", "2019+ (dual-band Ch-B)" },
        .year_count = 2,
    },
    /* ── Ford / Lincoln (NA) ─────────────────────────────────────────────── */
    /* The North American Ford/Lincoln entries below use 315 MHz OOK rolling
       code profiles. */
    {
        .make = "Ford / Lincoln (NA)",
        .model = "F-150 / F-250 / F-350 / Expedition (315 MHz)",
        .years = { "1999+" },
        .year_count = 1,
    },
    {
        .make = "Ford / Lincoln (NA)",
        .model = "Mustang / Explorer / Escape / Edge / Bronco (315 MHz)",
        .years = { "2001+" },
        .year_count = 1,
    },
    {
        .make = "Ford / Lincoln (NA)",
        .model = "Fusion / Taurus / Ranger / Focus NA (315 MHz)",
        .years = { "2000+" },
        .year_count = 1,
    },
    {
        .make = "Ford / Lincoln (NA)",
        .model = "Lincoln Navigator / MKZ / Corsair (315 MHz)",
        .years = { "2003+" },
        .year_count = 1,
    },
    /* ── Ford EU ─────────────────────────────────────────────────────────── */
    {
        .make = "Ford EU",
        .model = "Focus / Fiesta / Mondeo / C-Max (433 MHz)",
        .years = { "1998+" },
        .year_count = 1,
    },
    {
        .make = "Ford EU",
        .model = "Kuga / Galaxy / S-Max / Puma / EcoSport (433 MHz)",
        .years = { "2006+" },
        .year_count = 1,
    },
    /* ── Mazda ───────────────────────────────────────────────────────────── */
    {
        .make = "Mazda",
        .model = "Mazda3 / CX-3 / CX-5 (315 MHz)",
        .years = { "2017", "2018", "2019", "2020" },
        .year_count = 4,
    },
    {
        .make = "Mazda",
        .model = "Mazda2 Sedan (433 MHz)",
        .years = { "2017", "2018", "2019", "2020" },
        .year_count = 4,
    },
    /* ── Honda / Acura ───────────────────────────────────────────────────── */
    {
        .make = "Honda",
        .model = "Fit / City / Vezel (310-313 MHz)",
        .years = { "2016", "2017", "2018", "2019", "2020", "2021", "2022" },
        .year_count = 7,
    },
    {
        .make = "Honda",
        .model = "Brio EU (433 MHz)",
        .years = { "2016", "2017", "2018" },
        .year_count = 3,
    },
    /* ── Chrysler / Dodge / Jeep / RAM ──────────────────────────────────── */
    {
        .make = "Chrysler/Dodge/Jeep/RAM",
        .model = "Jeep / Dodge / RAM (older fixed, ~300 MHz)",
        .years = { "1997-2008" },
        .year_count = 1,
    },
    {
        .make = "Chrysler/Dodge/Jeep/RAM",
        .model = "RAM / Grand Cherokee / Charger (KeeLoq, 315 MHz)",
        .years = { "2009-2018" },
        .year_count = 1,
    },
    {
        .make = "Chrysler/Dodge/Jeep/RAM",
        .model = "Stellantis — RAM / Jeep / Dodge (KeeLoq, 433 MHz)",
        .years = { "2019+" },
        .year_count = 1,
    },
    /* ── GM (Chevrolet / GMC / Buick / Cadillac) ─────────────────────────── */
    {
        .make = "GM (Chevy/GMC/Buick/Cadillac)",
        .model = "Silverado / Sierra / Tahoe / Suburban (fixed, 318 MHz)",
        .years = { "1999-2006" },
        .year_count = 1,
    },
    {
        .make = "GM (Chevy/GMC/Buick/Cadillac)",
        .model = "Silverado / Equinox / Malibu / Tahoe (KeeLoq, 315 MHz)",
        .years = { "2007-2018" },
        .year_count = 1,
    },
    {
        .make = "GM (Chevy/GMC/Buick/Cadillac)",
        .model = "Malibu / Equinox / Trailblazer / Cadillac XT5 (KeeLoq, 433 MHz)",
        .years = { "2014+" },
        .year_count = 1,
    },
    /* ── Subaru (NA) ──────────────────────────────────────────────────────── */
    {
        .make = "Subaru (NA)",
        .model = "Outback / Forester / Impreza / Legacy / Crosstrek (312 MHz)",
        .years = { "2000-2014" },
        .year_count = 1,
    },
    {
        .make = "Subaru (NA)",
        .model = "Outback / Forester / Impreza / Crosstrek (915 MHz)",
        .years = { "2015+" },
        .year_count = 1,
    },
    /* ── Genesis ──────────────────────────────────────────────────────────── */
    {
        .make = "Genesis",
        .model = "G70 / G80 / G90 / GV70 / GV80 (433 MHz, 2FSK)",
        .years = { "2017+" },
        .year_count = 1,
    },
    /* ── VW / Audi / Skoda / SEAT (EU) ───────────────────────────────────── */
    {
        .make = "VW/Audi/Skoda/SEAT (EU)",
        .model = "Golf / Passat / Tiguan / A3 / A4 / Octavia / Ateca (433 MHz)",
        .years = { "1999-2019" },
        .year_count = 1,
    },
    {
        .make = "VW/Audi/Skoda/SEAT (EU)",
        .model = "Golf / Passat / Tiguan / A3 / A4 / Octavia / Ateca (868 MHz)",
        .years = { "2020+" },
        .year_count = 1,
    },
    /* ── Volkswagen (NA) ──────────────────────────────────────────────────── */
    {
        .make = "Volkswagen (NA)",
        .model = "Atlas / Jetta / Tiguan / Passat / Taos (315 MHz)",
        .years = { "2005+" },
        .year_count = 1,
    },
    /* ── BMW / Mercedes-Benz (EU) ─────────────────────────────────────────── */
    {
        .make = "BMW / Mercedes (EU)",
        .model = "BMW 3/5-Series / X3 / X5 / Mercedes C/E-Class (433 MHz)",
        .years = { "1998-2012" },
        .year_count = 1,
    },
    {
        .make = "BMW / Mercedes (EU)",
        .model = "BMW 3/5-Series / X3 / X5 / Mercedes C/E/A-Class (868 MHz)",
        .years = { "2012+" },
        .year_count = 1,
    },
    /* ── Peugeot / Citroen (EU) ───────────────────────────────────────────── */
    {
        .make = "Peugeot / Citroen (EU)",
        .model = "206/207/208/307/308 / C3/C4/C5 (433 MHz)",
        .years = { "2000-2019" },
        .year_count = 1,
    },
    {
        .make = "Peugeot / Citroen (EU)",
        .model = "208/308/3008 / C3/C4 Aircross (868 MHz)",
        .years = { "2020+" },
        .year_count = 1,
    },
    /* ── Renault (EU) ─────────────────────────────────────────────────────── */
    {
        .make = "Renault (EU)",
        .model = "Clio / Megane / Laguna / Captur / Kadjar / Zoe (433 MHz)",
        .years = { "1998+" },
        .year_count = 1,
    },
    /* ── Opel / Vauxhall (EU) ─────────────────────────────────────────────── */
    {
        .make = "Opel / Vauxhall (EU)",
        .model = "Astra / Corsa / Insignia / Mokka (433 MHz)",
        .years = { "1998-2019" },
        .year_count = 1,
    },
    {
        .make = "Opel / Vauxhall (EU)",
        .model = "Astra / Corsa / Insignia / Mokka (868 MHz)",
        .years = { "2020+" },
        .year_count = 1,
    },
    /* ── Fiat / Alfa Romeo (EU) ───────────────────────────────────────────── */
    {
        .make = "Fiat / Alfa Romeo (EU)",
        .model = "Fiat 500 / Punto / Bravo / Alfa Giulia / Stelvio / 159 (433 MHz)",
        .years = { "2000+" },
        .year_count = 1,
    },
    /* ── Volvo (EU) ───────────────────────────────────────────────────────── */
    {
        .make = "Volvo (EU)",
        .model = "S60 / V70 / XC60 / XC70 / XC90 / XC40 (433 MHz)",
        .years = { "2001-2019" },
        .year_count = 1,
    },
    {
        .make = "Volvo (EU)",
        .model = "S60 / V70 / XC60 / XC70 / XC90 / XC40 (868 MHz)",
        .years = { "2020+" },
        .year_count = 1,
    },
    /* ── Honda EU ─────────────────────────────────────────────────────────── */
    {
        .make = "Honda EU",
        .model = "Civic / CR-V / HR-V / Jazz / Accord EU (433 MHz)",
        .years = { "2001+" },
        .year_count = 1,
    },
    /* ── Nissan EU ────────────────────────────────────────────────────────── */
    {
        .make = "Nissan EU",
        .model = "Qashqai / Micra / X-Trail / Juke / Note (433 MHz)",
        .years = { "2003+" },
        .year_count = 1,
    },
    /* ── Subaru EU ────────────────────────────────────────────────────────── */
    {
        .make = "Subaru EU",
        .model = "Impreza / WRX / Forester / Legacy / Outback / Crosstrek (433 MHz)",
        .years = { "1998+" },
        .year_count = 1,
    },
    /* ── Mazda EU ─────────────────────────────────────────────────────────── */
    {
        .make = "Mazda EU",
        .model = "RX-8 / Mazda3 / Mazda6 / CX-5 / MX-5 (433 MHz)",
        .years = { "2003+" },
        .year_count = 1,
    },
    /* ── EU Gate & Garage (additional brands) ────────────────────────────── */
    {
        .make = "EU Gate & Garage",
        .model = "DoorHan / Hormann / Sommer / FAAC / BFT / Beninca (433 MHz)",
        .years = { "2000+" },
        .year_count = 1,
    },
    /* ── Security+ / LiftMaster ──────────────────────────────────────────── */
    {
        .make = "LiftMaster",
        .model = "Security+ 1.0 Garage (390 MHz)",
        .years = { "1995", "1996", "1997", "1998", "1999", "2000", "2001", "2002" },
        .year_count = 8,
    },
    {
        .make = "LiftMaster",
        .model = "Security+ 2.0 Garage / myQ (315 MHz)",
        .years = { "2010", "2011", "2012", "2013", "2014", "2015", "2016", "2017" },
        .year_count = 8,
    },
    /* ── CAME / Nice ─────────────────────────────────────────────────────── */
    {
        .make = "CAME",
        .model = "12-bit Gate Fob (EU 433)",
        .years = { "2005", "2008", "2010", "2012", "2015" },
        .year_count = 5,
    },
    {
        .make = "Nice",
        .model = "FLO / FloR Gate Fob (EU 433)",
        .years = { "2005", "2008", "2010", "2013", "2016" },
        .year_count = 5,
    },
    /* ── Generic KeeLoq ──────────────────────────────────────────────────── */
    {
        .make = "Generic",
        .model = "KeeLoq 315 MHz",
        .years = { "any" },
        .year_count = 1,
    },
    {
        .make = "Generic",
        .model = "KeeLoq 433 MHz",
        .years = { "any" },
        .year_count = 1,
    },
};
#else
const FlipperFcVehicle FLIPPER_FC_VEHICLES[] = {
    { .make = "Chrysler/Dodge/Jeep/RAM", .model = "315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Chrysler/Dodge/Jeep/RAM", .model = "433 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "GM (Chevy/GMC/Buick/Cadillac)", .model = "315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Ford / Lincoln (NA)", .model = "315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Fiat / Alfa Romeo (EU)", .model = "433 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Honda", .model = "315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Toyota", .model = "312 / 315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Lexus", .model = "315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Nissan", .model = "315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Hyundai", .model = "315 / 433 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Kia", .model = "315 / 433 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Subaru (NA)", .model = "315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Mazda", .model = "315 / 433 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Generic", .model = "KeeLoq 315 MHz", .years = { "any" }, .year_count = 1 },
    { .make = "Generic", .model = "KeeLoq 433 MHz", .years = { "any" }, .year_count = 1 },
};
#endif

const int FLIPPER_FC_VEHICLE_COUNT = (int)(sizeof(FLIPPER_FC_VEHICLES) / sizeof(FLIPPER_FC_VEHICLES[0]));

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBback profiles (FBK_P)                                                   */
/* ─────────────────────────────────────────────────────────────────────────── */
const FlipperFbkProfile FLIPPER_FBK_PROFILES[] = {
    {
        .key = "hy_kia_315",
        .name = "Hyundai/Kia 315 MHz (Omron)",
        .freqs = { 315.00f, 314.65f, 313.00f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 2,
        .seq = FbkSeqLoose,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "hy_kia_433",
        .name = "Hyundai/Kia 433 MHz (Omron)",
        .freqs = { 433.92f, 433.67f, 434.17f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 2,
        .seq = FbkSeqLoose,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "hy_ix20",
        .name = "Hyundai ix20 433 MHz (NXP)",
        .freqs = { 433.92f, 433.67f, 434.17f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 2,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "nis_latio",
        .name = "Nissan Latio 315 MHz",
        .freqs = { 315.00f, 314.35f, 314.89f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 2,
        .seq = FbkSeqStrict,
        .timeframe_s = 5,
        .note = "",
    },
    {
        .key = "nis_sylphy",
        .name = "Nissan Sylphy 315 MHz",
        .freqs = { 315.00f, 314.35f, 314.89f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 2,
        .seq = FbkSeqStrict,
        .timeframe_s = 8,
        .note = "",
    },
    {
        .key = "nis_navara",
        .name = "Nissan Navara 315 MHz",
        .freqs = { 315.00f, 314.35f, 314.89f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 2,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "toy_rush",
        .name = "Toyota Rush 315 MHz",
        .freqs = { 315.00f, 314.35f, 315.65f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 2,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "toy_wigo",
        .name = "Toyota Wigo S 315 MHz",
        .freqs = { 315.00f, 314.35f, 315.65f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 3,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "maz_315",
        .name = "Mazda 315 MHz (3-signal)",
        .freqs = { 314.35f, 315.00f, 313.00f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 3,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "maz_433",
        .name = "Mazda 433 MHz (3-signal)",
        .freqs = { 433.89f, 433.92f, 433.67f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 3,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "hon_310",
        .name = "Honda 310-313 MHz (5-signal)",
        .freqs = { 313.00f, 310.00f, 309.75f, 310.25f },
        .freq_count = 4,
        .mod = FlipperModOOK,
        .n_captures = 5,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
    {
        .key = "hon_433",
        .name = "Honda EU 433 MHz (5-signal)",
        .freqs = { 433.92f, 433.67f, 434.18f },
        .freq_count = 3,
        .mod = FlipperModOOK,
        .n_captures = 5,
        .seq = FbkSeqStrict,
        .timeframe_s = 0,
        .note = "",
    },
};

const int FLIPPER_FBK_PROFILE_COUNT = (int)(sizeof(FLIPPER_FBK_PROFILES) / sizeof(FLIPPER_FBK_PROFILES[0]));

/* ─────────────────────────────────────────────────────────────────────────── */
/* FOBback vehicle makes (FBK_V)                                              */
/* ─────────────────────────────────────────────────────────────────────────── */

/* Resolve profile keys at first use; this avoids a forward reference in the
   static model table. */
static inline const FlipperFbkProfile* FP(const char* k) {
    return flipper_fbk_profile_by_key(k);
}

static const FlipperFbkModel hyundai_models[] = {
    { "Elantra 2013-2015 (315 MHz)", NULL /* hy_kia_315 */ },
    { "Elantra 2015 (433 MHz)",      NULL /* hy_kia_433 */ },
    { "ix20 2010-2019 EU",           NULL /* hy_ix20    */ },
};
static const FlipperFbkModel kia_models[] = {
    { "Cerato K3 2012-2016 (315 MHz)", NULL },
    { "Cerato K3 2016-2018 (433 MHz)", NULL },
};
static const FlipperFbkModel nissan_models[] = {
    { "Latio 2007-2012",  NULL },
    { "Sylphy 2012-2019", NULL },
    { "Navara 2010",      NULL },
};
static const FlipperFbkModel toyota_models[] = {
    { "Rush 2017",   NULL },
    { "Wigo S 2017", NULL },
};
static const FlipperFbkModel mazda_models[] = {
    { "Mazda3 2018",          NULL },
    { "Mazda2 Sedan 2017-2020", NULL },
    { "CX-3 2019",            NULL },
    { "CX-5 2018",            NULL },
    { "Mazda2 (433 MHz)",     NULL },
};
static const FlipperFbkModel honda_models[] = {
    { "Fit (hybrid) 2016-2018", NULL },
    { "Fit 2018",               NULL },
    { "City 2017",              NULL },
    { "Vezel 2016-2022",        NULL },
    { "Brio EU 2016",           NULL },
};

/* Parallel profile-key arrays keep the static model table straightforward. */
static const char* hyundai_pkeys[] = { "hy_kia_315", "hy_kia_433", "hy_ix20" };
static const char* kia_pkeys[]     = { "hy_kia_315", "hy_kia_433" };
static const char* nissan_pkeys[]  = { "nis_latio", "nis_sylphy", "nis_navara" };
static const char* toyota_pkeys[]  = { "toy_rush", "toy_wigo" };
static const char* mazda_pkeys[]   = { "maz_315", "maz_315", "maz_315", "maz_315", "maz_433" };
static const char* honda_pkeys[]   = { "hon_310", "hon_310", "hon_310", "hon_310", "hon_433" };

const FlipperFbkMake FLIPPER_FBK_MAKES[] = {
    { "Hyundai",      "Asia/EU", {}, 3 },
    { "Kia",          "Asia",    {}, 2 },
    { "Nissan",       "Asia",    {}, 3 },
    { "Toyota",       "Asia",    {}, 2 },
    { "Mazda",        "Asia",    {}, 5 },
    { "Honda/Acura",  "Asia",    {}, 5 },
};

const int FLIPPER_FBK_MAKE_COUNT = (int)(sizeof(FLIPPER_FBK_MAKES) / sizeof(FLIPPER_FBK_MAKES[0]));

int flipper_fc_vehicle_count(void) {
    return FLIPPER_FC_VEHICLE_COUNT;
}

const FlipperFcVehicle* flipper_fc_vehicle_at(int i) {
    if(i < 0 || i >= FLIPPER_FC_VEHICLE_COUNT) return NULL;
    return &FLIPPER_FC_VEHICLES[i];
}

int flipper_fbk_make_count(void) {
    return FLIPPER_FBK_MAKE_COUNT;
}

const FlipperFbkMake* flipper_fbk_make_at(int i) {
    if(i < 0 || i >= FLIPPER_FBK_MAKE_COUNT) return NULL;
    return &FLIPPER_FBK_MAKES[i];
}

/* Look up a frequency or rollback profile by key. */
const FlipperFcProfile* flipper_fc_profile_by_key(const char* key) {
    for(int i = 0; i < FLIPPER_FC_PROFILE_COUNT; i++)
        if(strcmp(FLIPPER_FC_PROFILES[i].key, key) == 0)
            return &FLIPPER_FC_PROFILES[i];
    return NULL;
}

const FlipperFbkProfile* flipper_fbk_profile_by_key(const char* key) {
    for(int i = 0; i < FLIPPER_FBK_PROFILE_COUNT; i++)
        if(strcmp(FLIPPER_FBK_PROFILES[i].key, key) == 0)
            return &FLIPPER_FBK_PROFILES[i];
    return NULL;
}

/* Return the profile associated with a make/model pair, or NULL if invalid. */
const FlipperFbkProfile* flipper_fbk_model_profile(int make_idx, int model_idx) {
    static const char** pkey_table[] = {
        hyundai_pkeys, kia_pkeys, nissan_pkeys,
        toyota_pkeys,  mazda_pkeys, honda_pkeys,
    };
    if(make_idx < 0 || make_idx >= FLIPPER_FBK_MAKE_COUNT) return NULL;
    if(model_idx < 0 || model_idx >= FLIPPER_FBK_MAKES[make_idx].model_count) return NULL;
    return flipper_fbk_profile_by_key(pkey_table[make_idx][model_idx]);
}

/* Return the model name for a make/model pair, or NULL if invalid. */
const char* flipper_fbk_model_name(int make_idx, int model_idx) {
    static const FlipperFbkModel* model_table[] = {
        hyundai_models, kia_models, nissan_models,
        toyota_models,  mazda_models, honda_models,
    };
    static const int count_table[] = { 3, 2, 3, 2, 5, 5 };
    if(make_idx < 0 || make_idx >= FLIPPER_FBK_MAKE_COUNT) return NULL;
    if(model_idx < 0 || model_idx >= count_table[make_idx]) return NULL;
    return model_table[make_idx][model_idx].model;
}
