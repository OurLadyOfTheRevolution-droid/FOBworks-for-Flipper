#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── FOBclone frequency profile ──────────────────────────────────────────── */
#define FC_PROFILE_FREQS_MAX 4

typedef enum {
    FlipperModOOK,
    FlipperMod2FSK,
} FlipperModulation;

typedef struct {
    const char*   key;          /* profile key string, e.g. "na_315"          */
    const char*   name;         /* display name                                */
    float         freqs[FC_PROFILE_FREQS_MAX]; /* MHz, first is primary       */
    int           freq_count;
    FlipperModulation mod;
    int           sweep_min_khz;
    int           sweep_max_khz;
} FlipperFcProfile;

/* ── FOBclone vehicle entry ───────────────────────────────────────────────── */
#define FC_YEARS_MAX 8

typedef struct {
    const char*       make;
    const char*       model;
    const char*       years[FC_YEARS_MAX];
    int               year_count;
    const FlipperFcProfile* profile;
} FlipperFcVehicle;

/* ── FOBback profile ──────────────────────────────────────────────────────── */
#define FBK_FREQS_MAX 4

typedef enum {
    FbkSeqLoose,   /* any N captures */
    FbkSeqStrict,  /* N consecutive matching roll-counters */
} FbkSeqMode;

typedef struct {
    const char*  key;
    const char*  name;
    float        freqs[FBK_FREQS_MAX];
    int          freq_count;
    FlipperModulation mod;
    int          n_captures;   /* required capture count */
    FbkSeqMode   seq;
    int          timeframe_s;  /* max seconds between first and replay (0 = none) */
    const char*  note;
} FlipperFbkProfile;

/* ── FOBback vehicle model ────────────────────────────────────────────────── */
typedef struct {
    const char*        model;
    const FlipperFbkProfile* profile;
} FlipperFbkModel;

/* ── FOBback make entry ───────────────────────────────────────────────────── */
#define FBK_MODELS_MAX 8

typedef struct {
    const char*   make;
    const char*   region;
    FlipperFbkModel   models[FBK_MODELS_MAX];
    int           model_count;
} FlipperFbkMake;

/* ── Counts ───────────────────────────────────────────────────────────────── */
/* Globals live in flipper_vehicles.c (host tests + fw_catalog.fal). The
   device host FAP does not link that file; scenes use the accessors below,
   which forward into the catalog plugin. */
extern const int FLIPPER_FC_PROFILE_COUNT;
extern const int FLIPPER_FC_VEHICLE_COUNT;
extern const int FLIPPER_FBK_MAKE_COUNT;

/* ── Tables ───────────────────────────────────────────────────────────────── */
extern const FlipperFcProfile  FLIPPER_FC_PROFILES[];
extern const FlipperFcVehicle  FLIPPER_FC_VEHICLES[];
extern const FlipperFbkProfile FLIPPER_FBK_PROFILES[];
extern const FlipperFbkMake    FLIPPER_FBK_MAKES[];

/* ── Lookups ──────────────────────────────────────────────────────────────── */
int flipper_fc_vehicle_count(void);
const FlipperFcVehicle* flipper_fc_vehicle_at(int i);
int flipper_fbk_make_count(void);
const FlipperFbkMake* flipper_fbk_make_at(int i);
const FlipperFcProfile*  flipper_fc_profile_by_key(const char* key);
const FlipperFbkProfile* flipper_fbk_profile_by_key(const char* key);
const FlipperFbkProfile* flipper_fbk_model_profile(int make_idx, int model_idx);
const char*          flipper_fbk_model_name(int make_idx, int model_idx);
