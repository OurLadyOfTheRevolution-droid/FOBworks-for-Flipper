#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification.h>
#include <storage/storage.h>

#include "protocol/flipper_keeloq.h"
#include "protocol/flipper_decoders.h"
#include "protocol/flipper_vehicles.h"
#include "protocol/flipper_capture.h"
#include "protocol/flipper_library.h"
#include "protocol/flipper_rollingpwn.h"
#include "link/flipper_link.h"

/* Shared FOBscan tuning table (defined in flipper_fobscan_app.c) — used by both
   the FOBscan on-device Up/Down tuning and the Advanced Settings screen. */
extern const float FOBSCAN_FREQS[];
extern const int   FOBSCAN_FREQ_COUNT;
#define FOBSCAN_FREQ_DEFAULT 9   /* index of 433.92 MHz */

#define FLIPPER_VERSION        "1.3"
#define FLIPPER_APP_NAME       "FOBworks for Flipper"
#define FLIPPER_LIB_PATH       EXT_PATH("flipper_fobscan/library")
#define FLIPPER_SIGNAL_LIB_MAX 8
#define FOBSCAN_FREQ_MAX       24   /* compile-time cap for the shared freq table */

/* ── Scene IDs ────────────────────────────────────────────────────────────── */
typedef enum {
    FlipperSceneMainMenu,
    /* FOBscan */
    FlipperSceneFobscan,
    /* FOBclone */
    FlipperSceneFobcloneMake,
    FlipperSceneFobcloneModel,
    FlipperSceneFobcloneYear,
    FlipperSceneFobcloneCapture,
    /* FOBcatch */
    FlipperSceneFobcatchMake,
    FlipperSceneFobcatchModel,
    FlipperSceneFobcatchYear,
    FlipperSceneFobcatchActive,
    /* FOBback */
    FlipperSceneFobbackMake,
    FlipperSceneFobbackModel,
    FlipperSceneFobbackListen,
    /* Advanced Settings */
    FlipperSceneAdvSettings,
    /* Library */
    FlipperSceneLibrary,       /* Decoded / Raw category select                  */
    FlipperSceneLibraryList,   /* file list for the selected category            */
    FlipperSceneLibraryItem,   /* Send / Info / Recover / Delete actions         */
    FlipperSceneLibraryInfo,   /* signal detail (custom canvas)                  */
    FlipperSceneLibScope,      /* read-only saved waveform browser                */
    /* Standalone utilities promoted from the FOBscan workbench tabs */
    FlipperSceneFobsweep,      /* realtime RSSI / signal-level meter             */
    FlipperSceneFobprotos,     /* supported-protocol reference                   */
    FlipperSceneFobLoq,        /* KeeLoq manufacturer key store                  */
    FlipperSceneInfo,          /* generic text detail (custom canvas)            */
    FlipperSceneCredits,       /* exit credits screen (custom canvas)            */
    FlipperSceneFobpwnSetup,     /* experimental Honda receive-only setup          */
    FlipperSceneFobpwnRun,       /* experimental sequence-candidate listener       */
    FlipperSceneFobwatch,        /* continuous receive/decode with optional save   */
    FlipperSceneFoblabs,         /* live capture/decode timing metrics             */
    FlipperSceneFobhunt,         /* bounded RSSI sweep over supported frequencies  */
    FlipperSceneFobcrack,        /* one KeeLoq frame, listed key or serial         */
    FlipperSceneCount,
} FlipperScene;

/*
 * View IDs — kept minimal.  All submenu-style screens (main menu, make/model/year
 * pickers) reuse FlipperViewMenu.  Each custom-canvas screen gets its own slot.
 */
typedef enum {
    FlipperViewMenu,          /* Submenu widget — reused for ALL list screens   */
    FlipperViewVarList,       /* VariableItemList — year picker                  */
    FlipperViewFobscan,       /* Custom canvas — FOBscan workbench               */
    FlipperViewFobclone,      /* Custom canvas — FOBclone capture / replay       */
    FlipperViewFobcatch,      /* Custom canvas — FOBcatch active / jam           */
    FlipperViewFobback,       /* Custom canvas — FOBback listen / replay         */
    FlipperViewLibInfo,       /* Custom canvas — Library signal detail           */
    FlipperViewLibScope,      /* Custom canvas — saved waveform scope             */
    FlipperViewFobsweep,      /* Custom canvas — realtime RSSI meter             */
    FlipperViewInfo,          /* Custom canvas — generic scrollable text detail  */
    FlipperViewCredits,       /* Custom canvas — exit credits screen             */
    FlipperViewFobpwn,        /* Custom canvas — experimental receive-only view  */
    FlipperViewRxTool,       /* Shared receive-only watch/lab/hunt canvas       */
    FlipperViewLab,          /* Custom canvas — FOBcrack / lab analysis         */
    FlipperViewCount,
} FlipperView;

/* ── Custom events ────────────────────────────────────────────────────────── */
typedef enum {
    FlipperEventCaptureDone  = 0x100,
    FlipperEventCaptureTimeout,
    FlipperEventReplayDone,
    FlipperEventJamStart,
    FlipperEventJamStop,
    FlipperEventStatusTick,
    FlipperEventRetune,        /* FOBscan: freq/preset changed on-device        */
    FlipperEventViewInLibrary, /* FOBscan: jump to last auto-saved capture       */
    FlipperEventRangeSetup,    /* FOBscan: Left → enter custom-range setup       */
    FlipperEventRangeCancel,   /* FOBscan: cancel range setup, back to Scan      */
    FlipperEventRangeConfirm,  /* FOBscan: confirm range, start ranged sweep     */
    FlipperEventSweepStop,     /* FOBscan: OK during sweep → stop on landed freq */
    FlipperEventFobpwnConsent, /* FOBpwn: explicit consent to receive captures    */
} FlipperCustomEvent;

/* ── Advanced Settings (persisted to SD) ──────────────────────────────────── */
typedef struct {
    int               freq_idx;        /* index into FOBSCAN_FREQS               */
    FlipperPreset     preset;          /* OOK/FSK modulation                     */
    float             squelch_dbm;     /* RSSI gate for FOBscan bursts           */
    FlipperForceProto force_proto;     /* Auto or one forced decoder             */
    bool              autosave_decoded;/* auto-save decoded captures to library  */
    bool              autosave_raw;    /* auto-save undecoded bursts to library  */
    bool              lib_evict_oldest;/* library full: true=drop oldest, false=skip new */
    bool              dashboard_link;  /* alloc USB+UART dashboard links (off saves ~9KB heap at launch) */
} FlipperAdvSettings;

/* ── Mode-specific state ──────────────────────────────────────────────────── */
/* FOBscan UI mode.  The Left button walks Scan → RangeSet → (confirm) → Sweep;
   OK during Sweep drops back to Scan on the landed frequency. */
typedef enum {
    FobscanModeScan = 0,   /* single-frequency live capture                     */
    FobscanModeRangeSet,   /* picking a custom [min,max] sweep range             */
    FobscanModeSweep,      /* auto-sweep across the custom range, linger on hits */
} FobscanUiMode;

typedef struct {
    bool     scanning;
    float    rssi_dbm;
    float    freq_mhz;
    int      freq_idx;      /* index into fobscan freq table (on-device tuning) */
    FlipperPreset preset;   /* modulation preset (mirrors Advanced Settings)     */
    uint32_t cap_count;
    FlipperDecodeResult last_decode;
    bool     last_decode_valid;
    int      flash_ticks;   /* >0: draw "SIGNAL CAPTURED" banner, counts down    */
    bool     flash_decoded; /* last capture was decoded (vs raw burst)            */

    /* Custom-range sweep (Left button) */
    FobscanUiMode ui_mode;
    int      range_step;    /* RangeSet: 0=pick max, 1=pick min, 2=ready         */
    int      cursor_idx;    /* RangeSet: freq cursor being adjusted              */
    int      range_min_idx; /* confirmed sweep range low bound (table index)     */
    int      range_max_idx; /* confirmed sweep range high bound (table index)    */
    int      sweep_idx;     /* Sweep: current freq index within [min,max]        */
    int      linger;        /* Sweep: remaining ticks to dwell on a hot freq     */
} FlipperFobscanState;

/* FOBsweep — realtime signal-level meter.  Samples RSSI on the selected freq
   every status tick; auto-advance walks the shared freq table so the bar row
   fills into a rolling per-frequency spectrum with peak-hold. */
typedef struct {
    int      sel_idx;                    /* selected freq (index into table)   */
    bool     auto_advance;              /* OK toggles band auto-sweep          */
    float    rssi[FOBSCAN_FREQ_MAX];    /* last sample per frequency (dBm)     */
    float    peak[FOBSCAN_FREQ_MAX];    /* peak-hold per frequency (dBm)       */
    int      linger;                    /* remaining ticks to dwell on a hot freq */
    int      peak_idx;                  /* freq index holding the strongest peak  */
} FlipperFobsweepState;

typedef enum {
    FlipperRxToolWatch = 0,
    FlipperRxToolLabs,
    FlipperRxToolHunt,
} FlipperRxToolKind;

/* Shared scalar-only state for FOBwatch/FOBlabs/FOBhunt. The capture engine
   owns the pulse buffer; retaining only summary metrics keeps these tools
   memory-safe on the Flipper's constrained heap. */
typedef struct {
    FlipperRxToolKind kind;
    bool running;
    float rssi_dbm;
    uint32_t captures;
    uint32_t saved;
    bool save_failed;
    bool decode_valid;
    FlipperDecodeResult decode;
    int pulse_count;
    uint32_t te_us;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t mean_us;
    int low_idx;
    int high_idx;
    int cursor_idx;
    bool edit_high;
    int sweep_idx;
    float hunt_rssi[FOBSCAN_FREQ_MAX];
    float hunt_peak[FOBSCAN_FREQ_MAX];
    int peak_idx;
} FlipperRxToolState;

typedef struct {
    int         make_idx;
    int         model_idx;
    int         year_idx;
    const FlipperFcProfile* profile;
    /* Capture buffers are allocated one-at-a-time after the guided pickers
       close.  Embedding two ~2 KB pulse buffers made merely entering FOBclone
       request a ~4 KB contiguous heap block and OOM on real devices. */
    FlipperCaptureResult*   cap[2];
    int                 cap_count;
    bool                armed;
    bool                replay_ready;
    bool                tx_pending;
} FlipperFobcloneState;

typedef struct {
    int         make_idx;
    int         model_idx;
    int         year_idx;
    const FlipperFcProfile* profile;
    FlipperCaptureResult    cap;
    bool                armed;
    bool                jamming;
    bool                tx_pending;
    bool                jam_completed;
    float               jam_freq_mhz;
} FlipperFobcatchState;

/* RollBack captures held for the ordered replay sequence.  Sized to the largest
   profile n_captures (currently 5); each FlipperCaptureResult carries a ~2 KB
   pulse buffer, so this array dominates the guided-flow union — do not oversize.
   If a profile ever needs more captures, raise this in lockstep. */
#define FLIPPER_FBK_CAPS_MAX 5

typedef struct {
    int         make_idx;
    int         model_idx;
    const FlipperFbkProfile* profile;
    FlipperCaptureResult     caps[FLIPPER_FBK_CAPS_MAX];
    int                  cap_count;
    bool                 armed;
    bool                 ready;
    bool                 tx_pending;
} FlipperFobbackState;

/* FOBpwn — Honda rollback (capture consecutive burst → resync replay). */
#define FLIPPER_FPWN_CAPS_MAX ROLLINGPWN_MAX_CAPS
typedef struct {
    int               variant_idx;
    float             freq_mhz;
    FlipperPreset     preset;
    int               min_seq;
    RollingPwnFrame   frames[FLIPPER_FPWN_CAPS_MAX];
    FlipperCaptureResult caps[FLIPPER_FPWN_CAPS_MAX];
    int               cap_count;
    bool              consented;
    bool              armed;
    bool              candidate;
    bool              start_failed;
    bool              radio_owned;
    bool              tx_pending;
    FlipperForceProto previous_force_proto;
    FlipperPreset     previous_preset;
    float             previous_freq_mhz;
    float             previous_squelch_dbm;
} FlipperFobpwnState;

/* FOBcrack — one listen, then a listed manufacturer key or the serial. */
typedef struct {
    bool        armed;
    bool        running;
    bool        done;
    bool        found;
    int         bits;       /* frequency-table index while listening          */
    char        status[48];
    char        result[80];
} FlipperFobcrackState;

typedef char FlipperFobpwnStateFitsFobbackBudget[
    sizeof(FlipperFobpwnState) <= sizeof(FlipperFobbackState) ? 1 : -1];

/* The guided flows never run simultaneously, so they overlay one union.
   This union is the single largest term in the app (fobback.caps ~11 KB), so it
   is heap-allocated OFF the contiguous FlipperApp block (app->guided) — keeping
   the one big malloc() small enough to launch within Flipper's tight heap. */
typedef union {
    FlipperFobcloneState fobclone;
    FlipperFobcatchState fobcatch;
    FlipperFobbackState  fobback;
    FlipperFobpwnState   fobpwn;
    FlipperFobcrackState fobcrack;
} FlipperGuided;

/* ── Application context ─────────────────────────────────────────────────── */
typedef struct {
    SceneManager*     scene_manager;
    ViewDispatcher*   view_dispatcher;
    Gui*              gui;
    NotificationApp*  notifications;
    Storage*          storage;

    /* Capture engine */
    FlipperCaptureEngine* capture;

    /* Signal library (live RAM feed — dashboard/decoded history) */
    FlipperDecodeResult   library[FLIPPER_SIGNAL_LIB_MAX];
    int               library_count;
    FuriMutex*        library_mutex;

    /* Advanced Settings (applied to FOBscan; persisted to SD) */
    FlipperAdvSettings adv;

    /* On-device library browser (SD-backed .sub files) */
    bool                 lib_browse_raw;    /* which category is being browsed   */
    FlipperLibEntry      lib_entries[FLIPPER_LIB_LIST_MAX];
    int                  lib_entry_count;
    char                 lib_sel_name[FLIPPER_LIB_NAME_MAX];
    char                 lib_sel_protocol[24];
    bool                 lib_sel_decoded;
    FlipperCaptureResult lib_sel;           /* loaded selection (pulses+decode)  */
    FlipperPreset        lib_sel_preset;
    int                  lib_scope_index;
    int                  lib_scope_zoom;
    int                  lib_scope_offset;

    /* Mode state. FOBscan/FOBsweep are small and keep persistent on-device
       tuning, so they stay separate. Guided scenes share a lazily allocated
       union because the FOBback member contains the large capture buffers
       (~11 KB). FOBpwn stores only three decoded metadata frames. Each guided
       setup scene requests its own member size and clears it before use. */
    FlipperFobscanState  fobscan;
    FlipperFobsweepState fobsweep;
    FlipperRxToolState   rx_tool;
    FlipperGuided*       guided;   /* heap-allocated union (see FlipperGuided) */

    /* FOBscan → Library hand-off: name of the most recently auto-saved capture
       so the live feed can jump straight into the Library item screen. */
    char                 fobscan_last_saved[FLIPPER_LIB_NAME_MAX];
    bool                 fobscan_last_saved_decoded;
    bool                 fobscan_has_saved;

    /* Generic text-detail view (FOBprotos / FOBLoq results). */
    char                 info_title[32];
    char                 info_body[320];

    /* Widgets */
    Submenu*          submenu;
    VariableItemList* var_list;

    /* Custom canvas views */
    View*   fobscan_view;
    View*   fobclone_view;
    View*   fobcatch_view;
    View*   fobback_view;
    View*   libinfo_view;
    View*   libscope_view;
    View*   fobsweep_view;
    View*   rx_tool_view;
    View*   info_view;
    View*   credits_view;
    View*   fobpwn_view;
    View*   fobcrack_view;

    /* 500 ms status tick */
    FuriTimer* tick_timer;

    /* ── Dashboard control links (USB CDC + GPIO UART) ─────────────────── */
    FlipperLink* usb_link;
    FlipperLink* uart_link;
    uint32_t     boot_tick;          /* furi_get_tick() at app start          */
    uint32_t     hb_last_tick;       /* last heartbeat emit                    */

    /* Remote-control (headless) state — owns the radio only when no GUI
       scene is driving it (gui_radio_active == false). */
    /* Serializes all radio access driven by the two link RX threads (USB/UART)
       and guards reads of gui_radio_active.  GUI scenes own the radio while
       gui_radio_active is set, at which point dispatch refuses radio ops. */
    FuriMutex*           radio_mutex;
    bool                 gui_radio_active;
    bool                 remote_scanning;
    uint32_t             remote_link_id;
    float                remote_squelch_dbm;
    FlipperCaptureResult remote_last;
    bool                 remote_last_valid;
} FlipperApp;

/* Broadcast a pre-formatted line to every attached transport (link dispatch). */
void flipper_app_broadcast(FlipperApp* app, const char* line, size_t len);
void flipper_app_tx_done(void* ctx);
void flipper_app_gui_tx_done(void* ctx);

/* Guided-flow union lifecycle: allocate on entering a guided make-scene, free on
   return to the main menu (see FlipperGuided).  ensure() is idempotent. */
bool flipper_guided_ensure(FlipperApp* app, size_t bytes);
void flipper_guided_release(FlipperApp* app);

/* Load/save Advanced Settings from/to SD. */
void flipper_adv_settings_load(FlipperApp* app);
void flipper_adv_settings_save(FlipperApp* app);

/* Allocate/free the USB+UART dashboard links on demand.  Both are null-safe and
 * idempotent so they can be toggled at runtime from Advanced Settings. */
void flipper_links_ensure(FlipperApp* app);
void flipper_links_release(FlipperApp* app);

/* Library detail (Info) canvas callbacks (defined in the library scene). */
void flipper_libinfo_draw_cb (Canvas* c, void* ctx);
bool flipper_libinfo_input_cb(InputEvent* e, void* ctx);
void flipper_libscope_draw_cb(Canvas* c, void* ctx);
bool flipper_libscope_input_cb(InputEvent* e, void* ctx);

void flipper_fobsweep_draw_cb (Canvas* c, void* ctx);
bool flipper_fobsweep_input_cb(InputEvent* e, void* ctx);
void flipper_rx_tool_draw_cb(Canvas* c, void* ctx);
bool flipper_rx_tool_input_cb(InputEvent* e, void* ctx);
void flipper_info_draw_cb (Canvas* c, void* ctx);
bool flipper_info_input_cb(InputEvent* e, void* ctx);
void flipper_credits_draw_cb (Canvas* c, void* ctx);
bool flipper_credits_input_cb(InputEvent* e, void* ctx);

/* GUI radio ownership handoff (defined in link dispatch).  A scene MUST call
   acquire() BEFORE it starts the radio and release() AFTER it stops, so the
   remote RX thread never drives the CC1101 concurrently → furi_check. */
void flipper_app_gui_radio_acquire(FlipperApp* app);
void flipper_app_gui_radio_release(FlipperApp* app);

/* canvas draw / input callback declarations (defined in scene files) */
void flipper_fobscan_draw_cb (Canvas* c, void* ctx);
bool flipper_fobscan_input_cb(InputEvent* e, void* ctx);
void flipper_fobclone_draw_cb (Canvas* c, void* ctx);
bool flipper_fobclone_input_cb(InputEvent* e, void* ctx);
void flipper_fobcatch_draw_cb (Canvas* c, void* ctx);
bool flipper_fobcatch_input_cb(InputEvent* e, void* ctx);
void flipper_fobback_draw_cb (Canvas* c, void* ctx);
bool flipper_fobback_input_cb(InputEvent* e, void* ctx);
void flipper_fobpwn_draw_cb (Canvas* c, void* ctx);
bool flipper_fobpwn_input_cb(InputEvent* e, void* ctx);
void flipper_fobcrack_draw_cb (Canvas* c, void* ctx);
bool flipper_fobcrack_input_cb(InputEvent* e, void* ctx);

#include "scenes/flipper_scenes.h"
