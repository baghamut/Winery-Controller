// =============================================================================
//  ui_lvgl.cpp  –  LVGL v9 UI  (single screen, 4 panels, 480×320 landscape)
//
//  MASTER POWER UNIFICATION:
//    The Control panel previously showed per-SSR sliders and toggle switches.
//    It now shows ONE "Master Power" slider (0–100 %) that controls all
//    active SSRs simultaneously.  The old cbSsrSlider / cbSsrToggle callbacks
//    and SsrRow struct are replaced by a single cbMasterSlider callback.
//
//    The Monitor panel shows a "Load Status: XX%" row on the left column,
//    using STR_MON_LOAD_STATUS from ui_strings.h.
// =============================================================================
#include "ui_lvgl.h"
#include "config.h"
#include "state.h"
#include "control.h"
#include "sensors.h"
#include "ui_strings.h"
#include <lvgl.h>

extern const lv_img_dsc_t img_barrel;

static constexpr float BAR_TO_KPA = 100.0f;
static constexpr float KPA_TO_BAR = 0.01f;

extern "C" void        wifiApplyConfig(const char* ssid, const char* pass);
extern "C" const char* wifiGetSsid();
extern "C" const char* wifiGetPass();

extern const lv_font_t lv_font_montserrat_8;
extern const lv_font_t lv_font_montserrat_10;
extern const lv_font_t lv_font_montserrat_12;
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_16;
extern const lv_font_t lv_font_montserrat_18;
extern const lv_font_t lv_font_montserrat_20;
extern const lv_font_t lv_font_montserrat_22;
extern const lv_font_t lv_font_montserrat_16_bold;
extern const lv_font_t lv_font_montserrat_22_bold;

// ---------------------------------------------------------------------------
// Layout constants  (landscape 480×320)
// ---------------------------------------------------------------------------
#define UI_W          480
#define UI_H          320
#define HDR_H          36
#define CONTENT_Y     HDR_H
#define CONTENT_H     (UI_H - HDR_H)
#define CARD_OUTER_W  (UI_W - 16)
#define CARD_INNER_W  (CARD_OUTER_W - 2 * 8)
#define CARD_H        (CONTENT_H - 16)
#define CARD_INNER_H  (CARD_H - 2 * 8)
#define CTRL_ROW_H    36
#define CTRL_ROW_Y0   24
#define CTRL_ROW_DY   40
#define MON_ROW_H     18    // row height (px) – 18 px fits 7 rows in available space
#define MON_ROW_Y0    24    // y offset of first sensor row inside card
#define MON_ROW_DY    18    // row step (= MON_ROW_H, no inter-row gap)
#define MON_STOP_Y    206   // fixed y of STOP button inside card (unchanged)

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define CLR_BG       lv_color_hex(0x111111)
#define CLR_CARD     lv_color_hex(0x202020)
#define CLR_CARD_ROW lv_color_hex(0x202020)
#define CLR_ACCENT   lv_color_hex(0xFF7A1A)
#define CLR_DANGER   lv_color_hex(0xE02424)
#define CLR_TEXT     lv_color_hex(0xF5F5F5)
#define CLR_MUTED    lv_color_hex(0xB3B3B3)
#define CLR_GREEN    lv_color_hex(0x65FF7A)
#define CLR_BORDER   lv_color_hex(0x333333)
#define STR_GREEN    lv_color_hex(0x008B00)
#define BCK_CARD     lv_color_hex(0xEE3B3B)
#define CLR_DIST_BG  lv_color_hex(0x303030)
#define CLR_RECT_BG  lv_color_hex(0x303030)

// ---------------------------------------------------------------------------
// Power Slider
// ---------------------------------------------------------------------------

#define MASTER_LBL_X     4
#define MASTER_SLIDER_X  80
#define MASTER_SLIDER_W  300
#define MASTER_PCT_GAP   26
#define MASTER_SLIDER_H  12

// ---------------------------------------------------------------------------
// Widget handles – header
// ---------------------------------------------------------------------------
static lv_obj_t* scr_root   = nullptr;
static lv_obj_t* hdr_bar    = nullptr;
static lv_obj_t* hdr_status = nullptr;
static lv_obj_t* hdr_t1     = nullptr;
static lv_obj_t* hdr_ip     = nullptr;
static lv_obj_t* hdr_tmax   = nullptr;
static lv_obj_t* hdr_total  = nullptr;  // total distillate volume (moved from monitor)
static lv_obj_t* hdr_ip_btn = nullptr;

// ---------------------------------------------------------------------------
// Widget handles – panels
// ---------------------------------------------------------------------------
static lv_obj_t* pnl_mode   = nullptr;  // Screen 0: mode selection
static lv_obj_t* pnl_ctrl   = nullptr;  // Screen 1: control (Master Power)
static lv_obj_t* pnl_mon    = nullptr;  // Screen 2: live monitor
static lv_obj_t* pnl_wifi   = nullptr;  // Overlay: WiFi config
static lv_obj_t* pnl_tmax   = nullptr;  // Overlay: sensor max-temp editor

// ---------------------------------------------------------------------------
// Widget handles – Mode panel
// ---------------------------------------------------------------------------
static lv_obj_t* btn_mode_dist = nullptr;
static lv_obj_t* btn_mode_rect = nullptr;

// ---------------------------------------------------------------------------
// Widget handles – Control panel  (Master Power)
//   All individual SSR rows replaced by a single master power slider row.
// ---------------------------------------------------------------------------
static lv_obj_t* ctrl_title       = nullptr;  // "Distillation Control" / "Rectification Control"
static lv_obj_t* ctrl_start       = nullptr;  // START button
static lv_obj_t* ctrl_back        = nullptr;  // BACK button (returns to mode selection)
static lv_obj_t* ctrl_masterSlider = nullptr; // Master Power slider (0–100)
static lv_obj_t* ctrl_masterPct    = nullptr; // Label showing "XX%"

// ---------------------------------------------------------------------------
// Widget handles – Sensor max-temp buttons on Control panel
// ---------------------------------------------------------------------------
static lv_obj_t* btn_tmax_s[3];           // [0]=Pressure, [1]=Tank, [2]=Pillar
static lv_obj_t* lbl_tmax_s[3];           // labels inside those buttons
static int       s_activeTmaxSensor = 1;  // which sensor is currently being edited
static lv_obj_t* tmax_ta    = nullptr;    // text area in tmax panel
static lv_obj_t* tmax_kb    = nullptr;    // keyboard in tmax panel

// ---------------------------------------------------------------------------
// Widget handles – Monitor panel
//
//  Two-column layout, 7 rows each (18 px/row):
//    Left col  : Room / Kettle / Pillar1 / Pillar2 / Pillar3 / Dephlegmator / Reflux
//    Right col : Pressure / Level / Product Flow / Water Dephl. / Water Cond. / Product Cooler
//    Header    : Total volume (moved out of sensor grid)
// ---------------------------------------------------------------------------
static lv_obj_t* mon_t1Lbl        = nullptr;   // Room
static lv_obj_t* mon_t2Lbl        = nullptr;   // Kettle
static lv_obj_t* mon_t3Lbl        = nullptr;   // Pillar 1
static lv_obj_t* mon_pillar2Lbl   = nullptr;   // Pillar 2   (extended)
static lv_obj_t* mon_pillar3Lbl   = nullptr;   // Pillar 3   (extended)
static lv_obj_t* mon_dephlegmLbl  = nullptr;   // Dephlegmator (extended)
static lv_obj_t* mon_refluxLbl    = nullptr;   // Reflux Cond. (extended)
static lv_obj_t* mon_pLbl         = nullptr;   // Pressure
static lv_obj_t* mon_levelLbl     = nullptr;   // Level
static lv_obj_t* mon_flowLbl      = nullptr;   // Product Flow
static lv_obj_t* mon_waterDephlLbl = nullptr;  // Water Dephl. Flow (extended)
static lv_obj_t* mon_waterCondLbl  = nullptr;  // Water Cond. Flow  (extended)
static lv_obj_t* mon_productLbl    = nullptr;  // Product Cooler Temp (extended)
static lv_obj_t* mon_loadSlider   = nullptr;
static lv_obj_t* mon_loadPct      = nullptr;
static lv_obj_t* mon_stop         = nullptr;

// ---------------------------------------------------------------------------
// Widget handles – WiFi panel
// ---------------------------------------------------------------------------
static lv_obj_t* wifi_ssid_ta  = nullptr;
static lv_obj_t* wifi_pass_ta  = nullptr;
static lv_obj_t* wifi_kb       = nullptr;

// ---------------------------------------------------------------------------
// Refresh state
// ---------------------------------------------------------------------------
static volatile bool s_refreshRequested = false;
static lv_timer_t*   s_refreshTimer     = nullptr;

// Forward declarations
static void uiShowMainFromWifi();
static void uiShowWifiConfig();
static void showOnlyPanel(lv_obj_t* show);


// ===========================================================================
// HELPERS
// ===========================================================================

// Remove all default LVGL padding/border/shadow from a container widget.
// Needed for panels that are pure layout wrappers.
static void resetPanelStyle(lv_obj_t* obj)
{
    lv_obj_set_style_pad_all(obj,       0, 0);
    lv_obj_set_style_pad_row(obj,       0, 0);
    lv_obj_set_style_pad_column(obj,    0, 0);
    lv_obj_set_style_border_width(obj,  0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj,  0, 0);
}

// Create a dark rounded card container positioned absolutely on its parent.
static lv_obj_t* makeCard(lv_obj_t* parent,
                           lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, lv_coord_t h)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card,      CLR_CARD,     0);
    lv_obj_set_style_bg_opa(card,        LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card,  CLR_BORDER,   0);
    lv_obj_set_style_border_width(card,  1,            0);
    lv_obj_set_style_radius(card,        8,            0);
    lv_obj_set_style_pad_all(card,       8,            0);
    lv_obj_set_style_outline_width(card, 0,            0);
    lv_obj_set_style_shadow_width(card,  0,            0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

// Create a styled push-button with text and a click callback.
static lv_obj_t* makeBtn(lv_obj_t* parent,
                          const char* text,
                          lv_color_t bgColor, lv_color_t txtColor,
                          lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h,
                          lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn,      bgColor,      0);
    lv_obj_set_style_bg_opa(btn,        LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn,  0,            0);
    lv_obj_set_style_radius(btn,        10,           0);
    lv_obj_set_style_shadow_width(btn,  0,            0);
    lv_obj_set_style_outline_width(btn, 0,            0);
    lv_obj_set_style_pad_all(btn,       4,            0);
    lv_obj_set_style_text_font(btn,     &lv_font_montserrat_16, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, txtColor, 0);
    lv_obj_center(lbl);

    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}


// ===========================================================================
// MASTER POWER SLIDER CALLBACKS
// cbMasterSlider  – fires every drag tick (LV_EVENT_VALUE_CHANGED)
//                   updates state + both % labels + applies SSR immediately.
//                   NO NVS write here.
// cbMasterSliderReleased – fires once on finger-lift (LV_EVENT_RELEASED)
//                          writes NVS exactly once per gesture.
// Both sliders (ctrl_masterSlider and mon_loadSlider) use these two callbacks.
// ===========================================================================
static void cbMasterSlider(lv_event_t* e)
{
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);

    // Update both % labels immediately – whichever panel is visible
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)v);
    if (ctrl_masterPct) lv_label_set_text(ctrl_masterPct, buf);
    if (mon_loadPct)    lv_label_set_text(mon_loadPct,    buf);

    // Update state in-place – no NVS write
    stateLock();
    g_state.masterPower = (float)v;
    stateUnlock();

    // Apply SSR immediately so hardware responds without waiting for the loop
    applySsrFromState();
}

static void cbMasterSliderReleased(lv_event_t* e)
{
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);
    char buf[16];
    snprintf(buf, sizeof(buf), "MASTER:%d", (int)v);
    postCommand(buf);
}

// ===========================================================================
// HEADER
//   Fixed strip at the top of every screen.
//   Contains: STOPPED/RUNNING/SAFETY  |  Room temp  |  Max temp  |  IP
// ===========================================================================
static bool wifiIpLooksBad(const char* ipStr)
{
    return (!ipStr || !ipStr[0] || strcmp(ipStr, "0.0.0.0") == 0);
}

static void cbIpClicked(lv_event_t* e)
{
    LV_UNUSED(e);
    uiShowWifiConfig();
}

static void buildHeader()
{
    hdr_bar = lv_obj_create(scr_root);
    lv_obj_set_pos(hdr_bar, 8, 4);
    lv_obj_set_size(hdr_bar, CARD_OUTER_W, HDR_H - 8);
    lv_obj_set_style_bg_color(hdr_bar,     CLR_CARD,   0);
    lv_obj_set_style_bg_opa(hdr_bar,       LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr_bar, CLR_BORDER, 0);
    lv_obj_set_style_border_width(hdr_bar, 1, 0);
    lv_obj_set_style_radius(hdr_bar,       8, 0);
    lv_obj_set_style_outline_width(hdr_bar, 0, 0);
    lv_obj_set_style_shadow_width(hdr_bar,  0, 0);
    lv_obj_set_style_pad_all(hdr_bar,      4, 0);
    lv_obj_clear_flag(hdr_bar, LV_OBJ_FLAG_SCROLLABLE);

    // SPACE_EVENLY distributes: Status | T1 | Max | IP
    lv_obj_set_layout(hdr_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr_bar,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr_bar, 0, 0);

    // Status badge (STOPPED / RUNNING / SAFETY TRIP)
    hdr_status = lv_label_create(hdr_bar);
    lv_label_set_text(hdr_status, STR_STATUS_STOPPED);
    lv_obj_set_style_text_color(hdr_status, CLR_MUTED, 0);
    lv_obj_set_style_text_font(hdr_status,  &lv_font_montserrat_14, 0);
    lv_obj_set_width(hdr_status, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(hdr_status, LV_TEXT_ALIGN_CENTER, 0);

    // T1 – Room sensor (USER_1 state = offline/red)
    hdr_t1 = lv_label_create(hdr_bar);
    {
        char buf[20];
        snprintf(buf, sizeof(buf), "%s: --", STR_SENSOR_NAME1);
        lv_label_set_text(hdr_t1, buf);
    }
    lv_obj_set_style_text_color(hdr_t1, CLR_GREEN,  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hdr_t1, CLR_DANGER, LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_add_state(hdr_t1, LV_STATE_USER_1);   // boot: sensor offline
    lv_obj_set_style_text_font(hdr_t1, &lv_font_montserrat_14, 0);
    lv_obj_set_width(hdr_t1, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(hdr_t1, LV_TEXT_ALIGN_CENTER, 0);

    // Max temperature (or safety trip message)
    hdr_tmax = lv_label_create(hdr_bar);
    lv_label_set_text(hdr_tmax, "Max: --");
    lv_obj_set_style_text_color(hdr_tmax, CLR_MUTED, 0);
    lv_obj_set_style_text_font(hdr_tmax,  &lv_font_montserrat_14, 0);
    lv_obj_set_width(hdr_tmax, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(hdr_tmax, LV_TEXT_ALIGN_CENTER, 0);

    // Total distillate volume – hidden until a run starts or volume > 0
    hdr_total = lv_label_create(hdr_bar);
    lv_label_set_text(hdr_total, "");
    lv_obj_set_style_text_color(hdr_total, CLR_MUTED, 0);
    lv_obj_set_style_text_font(hdr_total,  &lv_font_montserrat_14, 0);
    lv_obj_set_width(hdr_total, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(hdr_total, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(hdr_total, LV_OBJ_FLAG_HIDDEN);  // hidden by default; flex ignores it

    // IP address button – tapping opens WiFi config panel
    hdr_ip_btn = lv_btn_create(hdr_bar);
    lv_obj_set_width(hdr_ip_btn,  LV_SIZE_CONTENT);
    lv_obj_set_height(hdr_ip_btn, HDR_H + 8);
    lv_obj_set_style_bg_opa(hdr_ip_btn,       LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_ip_btn,  0, 0);
    lv_obj_set_style_outline_width(hdr_ip_btn, 0, 0);
    lv_obj_set_style_shadow_width(hdr_ip_btn,  0, 0);
    lv_obj_set_style_pad_all(hdr_ip_btn,       0, 0);
    lv_obj_clear_flag(hdr_ip_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hdr_ip_btn, cbIpClicked, LV_EVENT_CLICKED, nullptr);

    hdr_ip = lv_label_create(hdr_ip_btn);
    lv_label_set_text(hdr_ip, "0.0.0.0");
    lv_obj_set_style_text_color(hdr_ip, CLR_GREEN, 0);
    lv_obj_set_style_text_font(hdr_ip,  &lv_font_montserrat_14, 0);
    lv_obj_center(hdr_ip);
}


// ===========================================================================
// MODE PANEL  (Screen 0)
//   Two large buttons: DISTILLATION and RECTIFICATION.
//   Pressing one sends MODE:1 or MODE:2 which transitions to the Control panel.
// ===========================================================================
static void cbModeDist(lv_event_t*) { handleCommand("MODE:1"); }
static void cbModeRect(lv_event_t*) { handleCommand("MODE:2"); }

static void buildModePanel()
{
    pnl_mode = lv_obj_create(scr_root);
    lv_obj_set_pos(pnl_mode, 0, CONTENT_Y);
    lv_obj_set_size(pnl_mode, UI_W, CONTENT_H);
    lv_obj_set_style_bg_color(pnl_mode, CLR_BG, 0);
    lv_obj_set_style_bg_opa(pnl_mode, LV_OPA_COVER, 0);
    resetPanelStyle(pnl_mode);
    lv_obj_clear_flag(pnl_mode, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t header_gap   = 8;
    const lv_coord_t card_gap     = 4;
    const lv_coord_t top_region_h = CONTENT_H * 45 / 100;
    const lv_coord_t top_card_h   = top_region_h - header_gap;
    const lv_coord_t top_card_y   = header_gap;
    const lv_coord_t bot_card_y   = top_card_y + top_card_h + card_gap;
    const lv_coord_t bot_card_h   = CONTENT_H - bot_card_y;

    // Top card: app name + barrel icon
    lv_obj_t* top_card = makeCard(pnl_mode, 8, top_card_y, CARD_OUTER_W, top_card_h);
    lv_obj_t* app_title = lv_label_create(top_card);
    lv_label_set_text(app_title, STR_APP_TITLE);
    lv_obj_set_style_text_font(app_title,  &lv_font_montserrat_22_bold, 0);
    lv_obj_set_style_text_color(app_title, CLR_ACCENT, 0);
    lv_obj_center(app_title);

    lv_obj_t* icon_left = lv_image_create(top_card);
    lv_image_set_src(icon_left, &img_barrel);
    lv_obj_align_to(icon_left, app_title, LV_ALIGN_OUT_LEFT_MID, -16, 0);

    // Bottom card: mode selection buttons
    lv_obj_t* card = makeCard(pnl_mode, 8, bot_card_y, CARD_OUTER_W, bot_card_h);
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, STR_TITLE_MODE_SEL);
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_16, 0);
    lv_obj_set_pos(title, 0, 0);

    const lv_coord_t btnY = 32;
    const lv_coord_t btnH = bot_card_h - btnY - 24;
    const lv_coord_t btnW = (CARD_INNER_W - 8) / 2;

    btn_mode_dist = makeBtn(card, STR_PROC_DIST, CLR_DIST_BG, CLR_ACCENT,
                            0,        btnY, btnW, btnH, cbModeDist);
    btn_mode_rect = makeBtn(card, STR_PROC_RECT, CLR_RECT_BG, CLR_ACCENT,
                            btnW + 8, btnY, btnW, btnH, cbModeRect);
}


// ===========================================================================
// TMAX PANEL  (Overlay)
//   Sensor max temperature editor.  Appears over the Control panel when a
//   limit button is tapped.  Has [-5][-1][textarea][+1][+5] + BACK + SAVE.
// ===========================================================================
static const lv_coord_t TMAX_ROW_HEIGHT = 44;
static const lv_coord_t TMAX_BTN_WIDTH  = 60;
static const lv_coord_t TMAX_BTN_HEIGHT = 36;
static const lv_coord_t TMAX_BTN_RADIUS = 4;

static void buildTmaxPanel()
{
    pnl_tmax = lv_obj_create(scr_root);
    lv_obj_set_pos(pnl_tmax, 0, CONTENT_Y);
    lv_obj_set_size(pnl_tmax, UI_W, CONTENT_H);
    lv_obj_set_style_bg_color(pnl_tmax, CLR_BG, 0);
    lv_obj_set_style_bg_opa(pnl_tmax, LV_OPA_COVER, 0);
    resetPanelStyle(pnl_tmax);
    lv_obj_clear_flag(pnl_tmax, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pnl_tmax, LV_OBJ_FLAG_HIDDEN);  // hidden by default

    // Card leaves bottom 140 px for the on-screen keyboard
    lv_obj_t* card = makeCard(pnl_tmax, 8, 4, CARD_OUTER_W, CONTENT_H - 140);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, STR_TMAX_PANEL_TITLE);
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(title, 0, 0);

    // Row: [-5][-1][value][+1][+5]
    lv_obj_t* grp = lv_obj_create(card);
    lv_obj_set_size(grp, lv_pct(100), 50);
    lv_obj_set_pos(grp, 0, 30);
    lv_obj_set_style_bg_opa(grp,       LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grp, 0, 0);
    lv_obj_set_style_pad_all(grp,      0, 0);
    lv_obj_clear_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(grp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Helper lambda: create a ±delta nudge button inside the flex row
    auto makeTmaxBtnInGrp = [&](const char* txt, int delta) {
        lv_obj_t* b = lv_btn_create(grp);
        lv_obj_set_size(b, TMAX_BTN_WIDTH, TMAX_BTN_HEIGHT);
        lv_obj_set_style_bg_color(b,     CLR_CARD_ROW, 0);
        lv_obj_set_style_bg_opa(b,       LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, CLR_BORDER, 0);
        lv_obj_set_style_radius(b,       TMAX_BTN_RADIUS, 0);

        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, CLR_TEXT, 0);
        lv_obj_set_style_text_font(l,  &lv_font_montserrat_16, 0);
        lv_obj_center(l);

        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            int d = (int)(intptr_t)lv_event_get_user_data(e);
            if (tmax_ta) {
                const char* cur = lv_textarea_get_text(tmax_ta);
                float v = atof(cur) + (float)d;
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", v);
                lv_textarea_set_text(tmax_ta, buf);
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)delta);
    };

    makeTmaxBtnInGrp("-5", -5);
    makeTmaxBtnInGrp("-1", -1);

    // Numeric text area
    tmax_ta = lv_textarea_create(grp);
    lv_textarea_set_one_line(tmax_ta, true);
    lv_textarea_set_accepted_chars(tmax_ta, "0123456789.");
    lv_obj_set_size(tmax_ta, 80, 40);
    lv_obj_set_style_text_align(tmax_ta,   LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(tmax_ta,    &lv_font_montserrat_16_bold, 0);
    lv_obj_set_style_bg_color(tmax_ta,     CLR_CARD_ROW, 0);
    lv_obj_set_style_text_color(tmax_ta,   CLR_TEXT, 0);
    lv_obj_set_style_border_width(tmax_ta, 1, 0);
    lv_obj_set_style_border_color(tmax_ta, CLR_ACCENT,
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(tmax_ta, [](lv_event_t* e) {
        if (tmax_kb) lv_keyboard_set_textarea(tmax_kb, tmax_ta);
    }, LV_EVENT_FOCUSED, nullptr);

    makeTmaxBtnInGrp("+1", 1);
    makeTmaxBtnInGrp("+5", 5);

    // On-screen number keyboard pinned to the bottom of the panel
    tmax_kb = lv_keyboard_create(pnl_tmax);
    lv_keyboard_set_mode(tmax_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(tmax_kb, UI_W, 130);
    lv_obj_align(tmax_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(tmax_kb, tmax_ta);

    const lv_coord_t gap   = 8;
    const lv_coord_t halfW = (CARD_INNER_W - gap) / 2;
    const lv_coord_t btnY  = 90;
    const lv_coord_t btnH  = 36;

    // BACK → returns to whichever main panel was active
    lv_obj_t* btn_back = makeBtn(card, STR_BTN_BACK, BCK_CARD, CLR_TEXT,
                                 0, btnY, halfW, btnH,
                                 [](lv_event_t*) { uiShowMainFromWifi(); });
    lv_obj_set_style_text_font(btn_back, &lv_font_montserrat_16_bold, 0);

    // SAVE → sends the correct threshold command then returns to main.
    // Pressure (s_activeTmaxSensor == 0) uses THRESH:D/R:PD: (bar).
    // Temperature sensors use TMAX:N:SET: (°C), N = 2 (Kettle) or 3 (Pillar 1).
    lv_obj_t* btn_save = makeBtn(card, STR_BTN_SAVE, STR_GREEN, CLR_TEXT,
                                 halfW + gap, btnY, halfW, btnH, [](lv_event_t*) {
        if (!tmax_ta) return;
        const char* val = lv_textarea_get_text(tmax_ta);
        if (s_activeTmaxSensor == 0) {
            // Pressure danger threshold – must use THRESH command
            stateLock();
            bool isRect = (g_state.processMode == 2);
            stateUnlock();
            float kpa = atof(val);
            char barStr[12];
            snprintf(barStr, sizeof(barStr), "%.4f", kpa * KPA_TO_BAR);
            handleCommand(String(isRect ? "THRESH:R:PD:" : "THRESH:D:PD:") + String(barStr));
        } else {
            // Temperature danger threshold (sensor index 2 = Kettle, 3 = Pillar 1)
            handleCommand("TMAX:" + String(s_activeTmaxSensor) + ":SET:" + String(val));
        }
        uiRequestRefresh();
        uiShowMainFromWifi();
    });
    lv_obj_set_style_text_font(btn_save, &lv_font_montserrat_16_bold, 0);
}


// ===========================================================================
// CONTROL PANEL  (Screen 1)
//   This panel is shown after a mode is selected and before START.
//   The operator sets Master Power here, then presses START.
//
//   MASTER POWER UNIFICATION: replaces 3 (or 2) individual SSR slider rows
//   with ONE row containing:
//     [Master Power label]  [slider 0–100]  [XX%]
//
//   Sensor limit buttons (Pressure / Tank / Pillar) remain unchanged.
//   BACK returns to mode selection (MODE:0).
//   START is disabled if masterPower == 0 or mode == 0 or safety tripped.
// ===========================================================================
static void cbStart(lv_event_t*)       { handleCommand("START"); }
static void cbControlBack(lv_event_t*) { handleCommand("MODE:0"); }

static void cbOpenTmaxConfig(lv_event_t* e)
{
    // Called when a sensor limit button is tapped.
    // Reads the current threshold value and pre-fills the editor.
    // user-data 0  → pressure (pressDanger)
    // user-data 2  → Kettle   (tempDanger[1])
    // user-data 3  → Pillar 1 (tempDanger[2])
    s_activeTmaxSensor = (int)(intptr_t)lv_event_get_user_data(e);

    stateLock();
    float val;
    if (s_activeTmaxSensor == 0) {
        // Pressure limit
        val = ((g_state.processMode == 2)
            ? g_state.threshRect.pressDanger
            : g_state.threshDist.pressDanger) * BAR_TO_KPA;
    } else {
        // Temperature limit – index is (s_activeTmaxSensor - 1): 1 = Kettle, 2 = Pillar
        val = (g_state.processMode == 2)
            ? g_state.threshRect.tempDanger[s_activeTmaxSensor - 1]
            : g_state.threshDist.tempDanger[s_activeTmaxSensor - 1];
    }
    stateUnlock();

    if (tmax_ta) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", val);
        lv_textarea_set_text(tmax_ta, buf);
    }
    showOnlyPanel(pnl_tmax);
}

static void buildControlPanel()
{
    pnl_ctrl = lv_obj_create(scr_root);
    lv_obj_set_pos(pnl_ctrl, 0, CONTENT_Y);
    lv_obj_set_size(pnl_ctrl, UI_W, CONTENT_H);
    lv_obj_set_style_bg_color(pnl_ctrl, CLR_BG, 0);
    lv_obj_set_style_bg_opa(pnl_ctrl, LV_OPA_COVER, 0);
    resetPanelStyle(pnl_ctrl);
    lv_obj_clear_flag(pnl_ctrl, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = makeCard(pnl_ctrl, 8, 8, CARD_OUTER_W, CARD_H);

    // Panel title: "Distillation Control" or "Rectification Control"
    ctrl_title = lv_label_create(card);
    lv_label_set_text(ctrl_title, STR_TITLE_CTRL);
    lv_obj_set_style_text_color(ctrl_title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(ctrl_title, &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(ctrl_title, 0, 0);

    // -----------------------------------------------------------------------
    // MASTER POWER ROW
    // -----------------------------------------------------------------------
    lv_obj_t* pwrRow = lv_obj_create(card);
    lv_obj_set_pos(pwrRow, 0, CTRL_ROW_Y0);
    lv_obj_set_size(pwrRow, lv_pct(100), CTRL_ROW_H + 6);
    lv_obj_set_style_bg_color(pwrRow, CLR_CARD_ROW, 0);
    lv_obj_set_style_bg_opa(pwrRow, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pwrRow, 6, 0);
    lv_obj_set_style_border_width(pwrRow, 0, 0);
    lv_obj_set_style_pad_all(pwrRow, 0, 0);
    lv_obj_set_style_outline_width(pwrRow, 0, 0);
    lv_obj_set_style_shadow_width(pwrRow, 0, 0);
    lv_obj_clear_flag(pwrRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* pwrLbl = lv_label_create(pwrRow);
    lv_label_set_text(pwrLbl, STR_MASTER_POWER_LABEL);
    lv_obj_set_style_text_color(pwrLbl, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(pwrLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(pwrLbl, LV_ALIGN_LEFT_MID, MASTER_LBL_X, 0);

    ctrl_masterSlider = lv_slider_create(pwrRow);
    lv_slider_set_range(ctrl_masterSlider, 0, 100);
    lv_slider_set_value(ctrl_masterSlider, 0, LV_ANIM_OFF);
    lv_obj_set_size(ctrl_masterSlider, MASTER_SLIDER_W, MASTER_SLIDER_H);
    lv_obj_set_pos(ctrl_masterSlider,
                   MASTER_SLIDER_X,
                   ((CTRL_ROW_H + 6) - MASTER_SLIDER_H) / 2);

    lv_obj_set_style_bg_color(ctrl_masterSlider, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctrl_masterSlider, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctrl_masterSlider,
                              CLR_ACCENT,
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ctrl_masterPct = lv_label_create(pwrRow);
    lv_label_set_text(ctrl_masterPct, "0%");
    lv_obj_set_style_text_color(ctrl_masterPct, CLR_MUTED, 0);
    lv_obj_set_style_text_font(ctrl_masterPct, &lv_font_montserrat_14, 0);
    lv_obj_align(ctrl_masterPct, LV_ALIGN_LEFT_MID,
                 MASTER_SLIDER_X + MASTER_SLIDER_W + MASTER_PCT_GAP,
                 0);

    // Register both callbacks – VALUE_CHANGED for live update, RELEASED for NVS write
    lv_obj_add_event_cb(ctrl_masterSlider, cbMasterSlider,         LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(ctrl_masterSlider, cbMasterSliderReleased, LV_EVENT_RELEASED,      nullptr);

    // -----------------------------------------------------------------------
    // SENSOR LIMITS AREA
    //   Positioned between Master row and BACK/START with guaranteed gap
    // -----------------------------------------------------------------------
    const lv_coord_t gap     = 8;
    const lv_coord_t halfW   = (CARD_INNER_W - gap) / 2;
    const lv_coord_t btnH    = 36;
    const lv_coord_t marginB = 4;

    // Bottom action row first
    lv_coord_t startY = 8 + CARD_INNER_H - btnH - marginB - 2;

    // Limits block placed higher and sized so it never overlaps START/BACK
    lv_coord_t rowY = CTRL_ROW_Y0 + CTRL_ROW_DY + 2;
    lv_coord_t limitsBottomGap = 10;
    lv_coord_t tmaxBoxH = startY - rowY - limitsBottomGap;

    lv_obj_t* tmaxBox = lv_obj_create(card);
    lv_obj_set_pos(tmaxBox, 0, rowY);
    lv_obj_set_size(tmaxBox, lv_pct(100), tmaxBoxH);
    lv_obj_set_style_bg_opa(tmaxBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tmaxBox, 0, 0);
    lv_obj_set_style_pad_all(tmaxBox, 0, 0);
    lv_obj_set_style_pad_row(tmaxBox, 6, 0);
    lv_obj_set_style_outline_width(tmaxBox, 0, 0);
    lv_obj_set_style_shadow_width(tmaxBox, 0, 0);
    lv_obj_clear_flag(tmaxBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(tmaxBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tmaxBox, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* lblSet = lv_label_create(tmaxBox);
    lv_label_set_text(lblSet, STR_LIMITS_LABEL);
    lv_obj_set_style_text_color(lblSet, CLR_MUTED, 0);
    lv_obj_set_style_text_font(lblSet, &lv_font_montserrat_14, 0);

    const char* smaxLabels[3] = { STR_SMAX1, STR_SMAX2, STR_SMAX3 };
    for (int i = 0; i < 3; i++) {
        btn_tmax_s[i] = lv_btn_create(tmaxBox);
        lv_obj_set_width(btn_tmax_s[i], lv_pct(100));
        lv_obj_set_height(btn_tmax_s[i], 34);
        lv_obj_set_style_bg_color(btn_tmax_s[i], lv_color_hex(0x383838), 0);
        lv_obj_set_style_bg_opa(btn_tmax_s[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn_tmax_s[i], 0, 0);
        lv_obj_set_style_radius(btn_tmax_s[i], 10, 0);
        lv_obj_set_style_shadow_width(btn_tmax_s[i], 0, 0);
        lv_obj_set_style_outline_width(btn_tmax_s[i], 0, 0);
        lv_obj_set_style_pad_all(btn_tmax_s[i], 4, 0);

        lbl_tmax_s[i] = lv_label_create(btn_tmax_s[i]);
        lv_label_set_text(lbl_tmax_s[i], smaxLabels[i]);
        lv_label_set_long_mode(lbl_tmax_s[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl_tmax_s[i], lv_pct(100));
        lv_obj_set_style_text_color(lbl_tmax_s[i], CLR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_tmax_s[i], &lv_font_montserrat_16_bold, 0);
        lv_obj_set_style_text_align(lbl_tmax_s[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl_tmax_s[i]);

        // user-data convention:
        //   0        → pressure sensor  (pressDanger, uses THRESH:D/R:PD command)
        //   2, 3     → temp sensor index 1, 2 (Kettle, Pillar 1 → tempDanger[1..2])
        lv_obj_add_event_cb(btn_tmax_s[i], cbOpenTmaxConfig, LV_EVENT_CLICKED,
                            (void*)(intptr_t)(i == 0 ? 0 : i + 1));
    }

    // -----------------------------------------------------------------------
    // BACK + START buttons at the bottom of the card
    // -----------------------------------------------------------------------
    ctrl_back = makeBtn(card, STR_BTN_BACK, BCK_CARD, CLR_TEXT,
                        0, startY, halfW, btnH, cbControlBack);
    lv_obj_set_style_text_font(ctrl_back, &lv_font_montserrat_16_bold, 0);

    ctrl_start = makeBtn(card, STR_BTN_START, STR_GREEN, CLR_TEXT,
                         halfW + gap, startY, halfW, btnH, cbStart);
    lv_obj_set_style_text_font(ctrl_start, &lv_font_montserrat_16_bold, 0);
}

// ===========================================================================
// MONITOR PANEL  (Screen 2)
//   Shown while the process is running.
//
//  Two-column layout inside one card:
//    Left  col (sensors): T1 / T2 / T3 / Total
//    Right col          : Pressure / Level / Flow / Load Status slider
//
//  SSR chips are REMOVED – the Load Status row now contains a read-only
//  slider identical in appearance to the Control panel Master Power slider.
//  It can be dragged to change Master Power just like the Control panel slider.
//
//  STOP button sits at the very bottom of the card.
// ===========================================================================
static void cbStop(lv_event_t*)
{
    // STOP already resets processMode to 0, masterPower to 0, clears the
    // safety latch, and calls stateSaveToNVS() — no need for a separate
    // MODE:0 which would trigger a second redundant NVS write.
    handleCommand("STOP");
}

static void buildMonitorPanel()
{
    pnl_mon = lv_obj_create(scr_root);
    lv_obj_set_pos(pnl_mon, 0, CONTENT_Y);
    lv_obj_set_size(pnl_mon, UI_W, CONTENT_H);
    lv_obj_set_style_bg_color(pnl_mon, CLR_BG, 0);
    lv_obj_set_style_bg_opa(pnl_mon, LV_OPA_COVER, 0);
    resetPanelStyle(pnl_mon);
    lv_obj_clear_flag(pnl_mon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = makeCard(pnl_mon, 8, 8, CARD_OUTER_W, CARD_H);

    // Panel title
    lv_obj_t* titleLbl = lv_label_create(card);
    lv_label_set_text(titleLbl, STR_TITLE_MONITOR);
    lv_obj_set_style_text_color(titleLbl, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(titleLbl,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(titleLbl, 0, 0);

    // -----------------------------------------------------------------------
    // Layout: two columns, 7 rows each at 18 px/row.
    //
    //  Card inner width = 448 px; gap between columns = 8 px.
    //  Column width = (448 - 8) / 2 = 220 px  (unchanged – preserves readability)
    //
    //  Available height for rows: MON_ROW_Y0 (24) to LOAD_ROW_Y (160) = 136 px.
    //  At 18 px/row: 7 rows × 18 px = 126 px ≤ 136 px ✓
    //
    //  LEFT  column: temperature sensors (core 3 always visible, extended 4 shown
    //                                      even when offline to keep layout stable)
    //  RIGHT column: pressure, level, flow sensors (core always visible,
    //                                                extended when assigned)
    //  Total volume: moved to header bar.
    // -----------------------------------------------------------------------
    const lv_coord_t COL_GAP  = 8;
    const lv_coord_t COL_W    = (CARD_INNER_W - COL_GAP) / 2;  // 220 px
    const lv_coord_t COL_R_X  = COL_W + COL_GAP;

    // Helper: label name/value row – same styling as before, 18 px tall.
    auto makeColRow = [&](lv_coord_t x, lv_coord_t y, lv_coord_t w,
                          const char* name, lv_obj_t** outLbl)
    {
        lv_obj_t* row = lv_obj_create(card);
        lv_obj_set_pos(row, x, y);
        lv_obj_set_size(row, w, MON_ROW_H);
        lv_obj_set_style_bg_color(row,      CLR_CARD_ROW, 0);
        lv_obj_set_style_bg_opa(row,        LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row,  0, 0);
        lv_obj_set_style_pad_all(row,       0, 0);
        lv_obj_set_style_outline_width(row, 0, 0);
        lv_obj_set_style_shadow_width(row,  0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* l1 = lv_label_create(row);
        lv_label_set_text(l1, name);
        lv_obj_set_style_text_color(l1, CLR_MUTED, 0);
        lv_obj_set_style_text_font(l1,  &lv_font_montserrat_14, 0);
        lv_obj_align(l1, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t* l2 = lv_label_create(row);
        lv_label_set_text(l2, "--");
        lv_obj_set_style_text_color(l2, CLR_TEXT,               LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(l2, CLR_DANGER,             LV_PART_MAIN | LV_STATE_USER_1);
        lv_obj_set_style_text_color(l2, lv_color_hex(0xFF7A1A), LV_PART_MAIN | LV_STATE_USER_2);
        lv_obj_set_style_text_font(l2,  &lv_font_montserrat_16, 0);  // 16 vs 18 saves ~2px height
        lv_obj_align(l2, LV_ALIGN_RIGHT_MID, -4, 0);
        *outLbl = l2;
    };

    // LEFT COLUMN – temperatures (rows 0–6)
    lv_coord_t ly = MON_ROW_Y0;
    makeColRow(0, ly, COL_W, STR_SENSOR_NAME1,  &mon_t1Lbl);       ly += MON_ROW_DY;
    makeColRow(0, ly, COL_W, STR_SENSOR_NAME2,  &mon_t2Lbl);       ly += MON_ROW_DY;
    makeColRow(0, ly, COL_W, STR_SENSOR_NAME3,  &mon_t3Lbl);       ly += MON_ROW_DY;
    makeColRow(0, ly, COL_W, STR_SENSOR_NAME4,  &mon_pillar2Lbl);  ly += MON_ROW_DY;
    makeColRow(0, ly, COL_W, STR_SENSOR_NAME5,  &mon_pillar3Lbl);  ly += MON_ROW_DY;
    makeColRow(0, ly, COL_W, STR_SENSOR_NAME6,  &mon_dephlegmLbl); ly += MON_ROW_DY;
    makeColRow(0, ly, COL_W, STR_SENSOR_NAME7,  &mon_refluxLbl);

    // RIGHT COLUMN – pressure / level / flow (rows 0–5, row 6 spare)
    lv_coord_t ry = MON_ROW_Y0;
    makeColRow(COL_R_X, ry, COL_W, STR_MON_PRESSURE,      &mon_pLbl);          ry += MON_ROW_DY;
    makeColRow(COL_R_X, ry, COL_W, STR_MON_LEVEL,         &mon_levelLbl);      ry += MON_ROW_DY;
    makeColRow(COL_R_X, ry, COL_W, STR_MON_FLOW,          &mon_flowLbl);       ry += MON_ROW_DY;
    makeColRow(COL_R_X, ry, COL_W, STR_SENSOR_WATER_DEPHL,&mon_waterDephlLbl); ry += MON_ROW_DY;
    makeColRow(COL_R_X, ry, COL_W, STR_SENSOR_WATER_COND, &mon_waterCondLbl);  ry += MON_ROW_DY;
    makeColRow(COL_R_X, ry, COL_W, STR_SENSOR_NAME8,      &mon_productLbl);

    // -----------------------------------------------------------------------
    // MASTER POWER ROW  (full-width, between sensor grid and STOP button)
    //
    //  This is the SAME control as the Control panel Master Power slider.
    //  It sends "MASTER:NN" via cbMasterSlider, so the operator can adjust
    //  power without leaving the Monitor screen.
    //
    //  Positioned at:  y = MON_STOP_Y - (CTRL_ROW_H+6) - 4
    //                      = 206 - 42 - 4 = 160
    //  Height:  CTRL_ROW_H + 6 = 42  (matches the Control panel row)
    //  Width:   full card inner width (CARD_INNER_W)
    //
    //  Layout:  ["Master Power" label] [slider] ["XX%" label]
    //           All geometry shared with Control via MASTER_* constants.
    // -----------------------------------------------------------------------
    const lv_coord_t LOAD_ROW_H = CTRL_ROW_H + 6;                // 42 px
    const lv_coord_t LOAD_ROW_Y = MON_STOP_Y - LOAD_ROW_H - 4;   // 160 px

    lv_obj_t* load_Row = lv_obj_create(card);
    lv_obj_set_pos(load_Row, 0, LOAD_ROW_Y);
    lv_obj_set_size(load_Row, CARD_INNER_W, LOAD_ROW_H);
    lv_obj_set_style_bg_color(load_Row,      CLR_CARD_ROW, 0);
    lv_obj_set_style_bg_opa(load_Row,        LV_OPA_COVER, 0);
    lv_obj_set_style_radius(load_Row,        6, 0);
    lv_obj_set_style_border_width(load_Row,  0, 0);
    lv_obj_set_style_pad_all(load_Row,       0, 0);
    lv_obj_set_style_outline_width(load_Row, 0, 0);
    lv_obj_set_style_shadow_width(load_Row,  0, 0);
    lv_obj_clear_flag(load_Row, LV_OBJ_FLAG_SCROLLABLE);

    // "Master Power" label on the left – accent colour, same as Control panel
    lv_obj_t* loadNameLbl = lv_label_create(load_Row);
    lv_label_set_text(loadNameLbl, STR_MASTER_POWER_LABEL);
    lv_obj_set_style_text_color(loadNameLbl, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(loadNameLbl,  &lv_font_montserrat_14, 0);
    lv_obj_align(loadNameLbl, LV_ALIGN_LEFT_MID, MASTER_LBL_X, 0);

    // Slider – geometry locked to Control panel via MASTER_* constants
    mon_loadSlider = lv_slider_create(load_Row);
    lv_slider_set_range(mon_loadSlider, 0, 100);
    lv_slider_set_value(mon_loadSlider, 0, LV_ANIM_OFF);
    lv_obj_set_size(mon_loadSlider, MASTER_SLIDER_W, MASTER_SLIDER_H);
    lv_obj_set_pos(mon_loadSlider,
                   MASTER_SLIDER_X,
                   (LOAD_ROW_H - MASTER_SLIDER_H) / 2);
    lv_obj_set_style_bg_color(mon_loadSlider, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mon_loadSlider,   LV_OPA_50,  LV_PART_MAIN);
    lv_obj_set_style_bg_color(mon_loadSlider,
                              CLR_ACCENT,
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(mon_loadSlider, cbMasterSlider, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(mon_loadSlider, cbMasterSliderReleased, LV_EVENT_RELEASED, nullptr);

    // Percentage label – same offset as Control: slider_x + slider_w + gap
    mon_loadPct = lv_label_create(load_Row);
    lv_label_set_text(mon_loadPct, "0%");
    lv_obj_set_style_text_color(mon_loadPct, CLR_MUTED, 0);
    lv_obj_set_style_text_font(mon_loadPct,  &lv_font_montserrat_14, 0);
    lv_obj_align(mon_loadPct, LV_ALIGN_LEFT_MID,
                 MASTER_SLIDER_X + MASTER_SLIDER_W + MASTER_PCT_GAP,
                 0);

    // -----------------------------------------------------------------------
    // STOP button – full-width, bottom of card
    // -----------------------------------------------------------------------
    mon_stop = makeBtn(card, STR_BTN_STOP, CLR_DANGER, CLR_TEXT,
                       0, MON_STOP_Y, CARD_INNER_W, 32, cbStop);
    lv_obj_set_style_text_font(mon_stop, &lv_font_montserrat_16_bold, 0);
}


// ===========================================================================
// URL encoding helper  (used by cbWifiSave)
//   Percent-encodes any character that is not unreserved per RFC 3986.
//   Required because the WIFI:SET:ssid:pass command uses ':' as a delimiter;
//   a raw colon in the SSID or password would corrupt the parse in control.cpp.
//   control.cpp::urlDecodeString() decodes the result on the other end.
// ===========================================================================
static String urlEncode(const char* src)
{
    if (!src) return String();
    String out;
    out.reserve(strlen(src) * 3);   // worst-case: every byte encoded
    for (const char* p = src; *p; ++p) {
        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
            out += buf;
        }
    }
    return out;
}

// ===========================================================================
// WIFI PANEL  (Overlay)
//   Shown when the IP label in the header is tapped.
//   Two text areas (SSID / password) + BACK + SAVE.
// ===========================================================================
static void cbWifiBack(lv_event_t*)  { uiShowMainFromWifi(); }
static void cbWifiSave(lv_event_t*)
{
    // Read both text areas, percent-encode each field, then send WIFI:SET.
    // Encoding is required because ':' is the command delimiter; a raw colon
    // in the SSID or password would cause control.cpp to split at the wrong
    // position.  control.cpp::urlDecodeString() decodes both fields.
    const char* s = wifi_ssid_ta ? lv_textarea_get_text(wifi_ssid_ta) : "";
    const char* p = wifi_pass_ta ? lv_textarea_get_text(wifi_pass_ta) : "";
    handleCommand("WIFI:SET:" + urlEncode(s) + ":" + urlEncode(p));
    uiShowMainFromWifi();
}

static void cbWifiTaFocus(lv_event_t* e)
{
    // When a text area gains focus, attach the shared keyboard to it
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (wifi_kb) lv_keyboard_set_textarea(wifi_kb, ta);
}

static void buildWifiPanel()
{
    pnl_wifi = lv_obj_create(scr_root);
    lv_obj_set_pos(pnl_wifi, 0, CONTENT_Y);
    lv_obj_set_size(pnl_wifi, UI_W, CONTENT_H);
    lv_obj_set_style_bg_color(pnl_wifi, CLR_BG, 0);
    lv_obj_set_style_bg_opa(pnl_wifi, LV_OPA_COVER, 0);
    resetPanelStyle(pnl_wifi);
    lv_obj_clear_flag(pnl_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pnl_wifi, LV_OBJ_FLAG_HIDDEN);  // hidden by default

    lv_obj_t* card = makeCard(pnl_wifi, 8, 4, CARD_OUTER_W, CONTENT_H - 140);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, STR_TITLE_WIFI_SETUP);
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(title, 0, 0);

    // Helper: a labelled text area row
    auto makeWifiRow = [&](const char* label, lv_coord_t y,
                           lv_obj_t** outTa, bool isPassword) {
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, CLR_MUTED, 0);
        lv_obj_set_style_text_font(lbl,  &lv_font_montserrat_14, 0);
        lv_obj_set_pos(lbl, 0, y);

        *outTa = lv_textarea_create(card);
        lv_textarea_set_one_line(*outTa, true);
        lv_obj_set_size(*outTa, CARD_INNER_W - 50, 30);
        lv_obj_set_pos(*outTa, 50, y - 4);
        lv_obj_set_style_bg_color(*outTa,   CLR_CARD_ROW, 0);
        lv_obj_set_style_text_color(*outTa, CLR_TEXT, 0);
        if (isPassword) lv_textarea_set_password_mode(*outTa, true);
        lv_obj_add_event_cb(*outTa, cbWifiTaFocus, LV_EVENT_FOCUSED, nullptr);
    };

    makeWifiRow(STR_WIFI_SSID_LABEL, 30, &wifi_ssid_ta, false);
    makeWifiRow(STR_WIFI_PASS_LABEL, 72, &wifi_pass_ta, true);

    // Pre-fill SSID with the currently active value
    if (wifi_ssid_ta && wifiGetSsid())
        lv_textarea_set_text(wifi_ssid_ta, wifiGetSsid());
    if (wifi_pass_ta && wifiGetPass())
        lv_textarea_set_text(wifi_pass_ta, wifiGetPass());

    // Shared keyboard
    wifi_kb = lv_keyboard_create(pnl_wifi);
    lv_obj_set_size(wifi_kb, UI_W, 130);
    lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_kb, wifi_ssid_ta);

    const lv_coord_t gap   = 8;
    const lv_coord_t halfW = (CARD_INNER_W - gap) / 2;
    const lv_coord_t btnY  = 122;
    const lv_coord_t btnH  = 32;

    lv_obj_t* btn_back = makeBtn(card, STR_BTN_BACK, BCK_CARD, CLR_TEXT,
                                 0, btnY, halfW, btnH, cbWifiBack);
    lv_obj_set_style_text_font(btn_back, &lv_font_montserrat_16_bold, 0);

    lv_obj_t* btn_save = makeBtn(card, STR_BTN_SAVE, STR_GREEN, CLR_TEXT,
                                 halfW + gap, btnY, halfW, btnH, cbWifiSave);
    lv_obj_set_style_text_font(btn_save, &lv_font_montserrat_16_bold, 0);
}


// ===========================================================================
// PANEL NAVIGATION
// ===========================================================================

// Hide all panels except `show`.
static void showOnlyPanel(lv_obj_t* show)
{
    lv_obj_t* panels[] = { pnl_mode, pnl_ctrl, pnl_mon, pnl_wifi, pnl_tmax };
    for (auto* p : panels) {
        if (!p) continue;
        if (p == show) lv_obj_clear_flag(p, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(p,   LV_OBJ_FLAG_HIDDEN);
    }
}

// Restore the correct main panel after closing the WiFi or Tmax overlay.
static void uiShowMainFromWifi()
{
    stateLock();
    int  pm      = g_state.processMode;
    bool running = g_state.isRunning;
    stateUnlock();

    if (pm == 0)       showOnlyPanel(pnl_mode);
    else if (!running) showOnlyPanel(pnl_ctrl);
    else               showOnlyPanel(pnl_mon);
}

static void uiShowWifiConfig()
{
    showOnlyPanel(pnl_wifi);
}


// ===========================================================================
// uiInit
//   Build all LVGL screens and show the correct initial panel.
//   Called once from setup() after LVGL and touch have been initialised.
// ===========================================================================
void uiInit()
{
    scr_root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_root, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr_root, LV_OPA_COVER, 0);
    lv_obj_set_size(scr_root, UI_W, UI_H);
    resetPanelStyle(scr_root);
    lv_obj_set_style_text_font(scr_root, &lv_font_montserrat_14, 0);
    lv_scr_load(scr_root);

    buildHeader();
    buildModePanel();
    buildControlPanel();
    buildMonitorPanel();
    buildWifiPanel();
    buildTmaxPanel();

    // Resume to the correct panel based on persisted state
    // (e.g. after a power-glitch auto-restore the process may already be running)
    {
        int  pm      = g_state.processMode;
        bool running = g_state.isRunning;
        if (pm == 0)       showOnlyPanel(pnl_mode);
        else if (!running) showOnlyPanel(pnl_ctrl);
        else               showOnlyPanel(pnl_mon);
    }

    // Periodic refresh timer – checks the flag set by uiRequestRefresh()
    // and calls uiRefreshFromState() only when a refresh is needed.
    s_refreshTimer = lv_timer_create([](lv_timer_t*) {
        if (s_refreshRequested) {
            s_refreshRequested = false;
            uiRefreshFromState();
        }
    }, LVGL_STATE_REFRESH_MS, nullptr);
}


// ===========================================================================
// uiRequestRefresh
//   Thread-safe: sets a flag consumed by the LVGL timer.
//   Safe to call from any task (sensorsTask, controlTask, loop()).
// ===========================================================================
void uiRequestRefresh()
{
    s_refreshRequested = true;
}


// ===========================================================================
// uiRefreshFromState
//   Reads a snapshot of g_state and updates all visible widgets.
//   Must only be called from the LVGL task/timer (Core 1) to avoid
//   concurrent modification of LVGL objects.
// ===========================================================================
void uiRefreshFromState()
{
    // Take a single locked snapshot to minimise the time the mutex is held.
    stateLock();
    AppState s = g_state;
    stateUnlock();

    // -----------------------------------------------------------------------
    // Header: status badge
    // -----------------------------------------------------------------------
    if (s.safetyTripped) {
        lv_label_set_text(hdr_status, STR_STATUS_SAFETY);
        lv_obj_set_style_text_color(hdr_status, CLR_DANGER, 0);
    } else if (s.isRunning) {
        lv_label_set_text(hdr_status, STR_STATUS_RUNNING);
        lv_obj_set_style_text_color(hdr_status, CLR_GREEN, 0);
    } else {
        lv_label_set_text(hdr_status, STR_STATUS_STOPPED);
        lv_obj_set_style_text_color(hdr_status, CLR_MUTED, 0);
    }

    // Header: Room temperature (T1)
    {
        char tb[32];
        if (s.roomTemp <= TEMP_OFFLINE_THRESH) {
            snprintf(tb, sizeof(tb), "%s: --", STR_SENSOR_NAME1);
            lv_obj_add_state(hdr_t1, LV_STATE_USER_1);
        } else {
            snprintf(tb, sizeof(tb), STR_HDR_T1_FMT, STR_SENSOR_NAME1, s.roomTemp);
            lv_obj_remove_state(hdr_t1, LV_STATE_USER_1);
        }
        lv_label_set_text(hdr_t1, tb);
    }

    // Header: IP address (red if not connected, green if connected)
    if (hdr_ip) {
        const char* ipStr = s.ip.c_str();
        bool bad = wifiIpLooksBad(ipStr);
        lv_label_set_text(hdr_ip, (ipStr && ipStr[0]) ? ipStr : "0.0.0.0");
        lv_obj_set_style_text_color(hdr_ip, bad ? CLR_DANGER : CLR_GREEN, 0);
    }

    // Header: max current sensor temp (or safety message)
    if (hdr_tmax) {
        if (s.safetyTripped && s.safetyMessage.length() > 0) {
            lv_label_set_text(hdr_tmax, s.safetyMessage.c_str());
            lv_obj_set_style_text_color(hdr_tmax, CLR_DANGER, 0);
        } else {
            float maxTemp = TEMP_OFFLINE_THRESH;
            const float allT[] = { s.roomTemp, s.kettleTemp, s.pillar1Temp,
                                   s.pillar2Temp, s.pillar3Temp,
                                   s.dephlegmTemp, s.refluxTemp, s.productTemp };
            for (float t : allT) if (t > TEMP_OFFLINE_THRESH && t > maxTemp) maxTemp = t;
            char tb[24];
            if (maxTemp <= TEMP_OFFLINE_THRESH)
                snprintf(tb, sizeof(tb), "%s: --", STR_MAX_PREFIX);
            else
                snprintf(tb, sizeof(tb), "%s: %.1f%s", STR_MAX_PREFIX, maxTemp, STR_UNIT_DEGC);
            lv_label_set_text(hdr_tmax, tb);
            lv_obj_set_style_text_color(hdr_tmax, CLR_MUTED, 0);
        }
    }

    // -----------------------------------------------------------------------
    // Panel visibility (only switch when WiFi/Tmax overlay is not showing)
    // -----------------------------------------------------------------------
    bool wifiVis = pnl_wifi && !lv_obj_has_flag(pnl_wifi, LV_OBJ_FLAG_HIDDEN);
    bool tmaxVis = pnl_tmax && !lv_obj_has_flag(pnl_tmax, LV_OBJ_FLAG_HIDDEN);

    if (!wifiVis && !tmaxVis) {
        if (s.processMode == 0)              showOnlyPanel(pnl_mode);
        else if (!s.isRunning)               showOnlyPanel(pnl_ctrl);
        else                                 showOnlyPanel(pnl_mon);
    }

    // -----------------------------------------------------------------------
    // Mode panel: highlight the active mode button
    // -----------------------------------------------------------------------
    if (btn_mode_dist && btn_mode_rect) {
        bool distOn = (s.processMode == 1);
        bool rectOn = (s.processMode == 2);
        lv_obj_set_style_bg_color(btn_mode_dist,
            distOn ? CLR_ACCENT : CLR_DIST_BG, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn_mode_dist, 0),
            distOn ? lv_color_hex(0x111111) : CLR_ACCENT, 0);
        lv_obj_set_style_bg_color(btn_mode_rect,
            rectOn ? CLR_ACCENT : CLR_RECT_BG, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn_mode_rect, 0),
            rectOn ? lv_color_hex(0x111111) : CLR_ACCENT, 0);
    }

    // -----------------------------------------------------------------------
    // Control panel: title
    // -----------------------------------------------------------------------
    if (ctrl_title) {
        if      (s.processMode == 1) lv_label_set_text(ctrl_title, STR_TITLE_CTRL_DIST);
        else if (s.processMode == 2) lv_label_set_text(ctrl_title, STR_TITLE_CTRL_RECT);
        else                          lv_label_set_text(ctrl_title, STR_TITLE_CTRL);
    }

    // -----------------------------------------------------------------------
    // Control panel: Master Power slider + percentage label
    //   Only update if the slider is not currently being dragged.
    //   (If the user is actively dragging, the cbMasterSlider callback already
    //    updates both the state and the label, so a forced update here would
    //    cause a visible jump.)
    // -----------------------------------------------------------------------
    if (ctrl_masterSlider) {
        int32_t curSliderVal = lv_slider_get_value(ctrl_masterSlider);
        int32_t stateVal     = (int32_t)s.masterPower;
        if (curSliderVal != stateVal) {
            lv_slider_set_value(ctrl_masterSlider, stateVal, LV_ANIM_OFF);
        }
    }
    if (ctrl_masterPct) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f%%", s.masterPower);
        lv_label_set_text(ctrl_masterPct, buf);
    }

    // -----------------------------------------------------------------------
    // Control panel: sensor limit buttons (Pressure | Tank | Pillar)
    //   Show "SensorName: currentDangerValue" so the operator sees the
    //   current setting at a glance without opening the editor.
    // -----------------------------------------------------------------------
    if (lbl_tmax_s[0]) {
        const char* smaxLabels[3] = { STR_SMAX1, STR_SMAX2, STR_SMAX3 };
        const SensorThresholds& thr = (s.processMode == 2) ? s.threshRect : s.threshDist;
        for (int i = 0; i < 3; i++) {
            char buf[48];
            if (i == 0) {
                // Pressure danger threshold
                snprintf(buf, sizeof(buf), "%s: %.1f kPa", smaxLabels[0], thr.pressDanger * BAR_TO_KPA);
            } else {
                // tempDanger[1]=Tank, tempDanger[2]=Pillar
                float val = thr.tempDanger[i];
                snprintf(buf, sizeof(buf), "%s: %.1f%s", smaxLabels[i], val, STR_UNIT_DEGC);
            }
            lv_label_set_text(lbl_tmax_s[i], buf);
        }
    }

    // -----------------------------------------------------------------------
    // Control panel: START button
    //   Enabled only when: mode is set AND masterPower > 0 AND not already
    //   running AND safety is not tripped.
    // -----------------------------------------------------------------------
    if (ctrl_start) {
        bool pmOk      = (s.processMode == 1 || s.processMode == 2);
        bool powerOk   = (s.masterPower > 0.0f);
        bool canStart  = pmOk && powerOk && !s.isRunning && !s.safetyTripped;
        if (canStart) lv_obj_clear_state(ctrl_start, LV_STATE_DISABLED);
        else          lv_obj_add_state(ctrl_start,   LV_STATE_DISABLED);
    }

    // -----------------------------------------------------------------------
    // Monitor panel: sensor readings
    //   USER_1 state → danger (red),  USER_2 state → warn (orange)
    //   setThreshLabel: for core sensors – offline = red "---"
    //   setExtLabel:    for extended sensors – offline = muted "---" (not an error)
    // -----------------------------------------------------------------------
    const SensorThresholds& thr = (s.processMode == 2) ? s.threshRect : s.threshDist;

    // Core sensor helper – shows offline in red (USER_1)
    auto setThreshLabel = [&](lv_obj_t* lbl, float val, float warn, float danger) {
        if (!lbl) return;
        char b[24];
        if (val <= TEMP_OFFLINE_THRESH) {
            snprintf(b, sizeof(b), "%s", STR_OFFLINE);
            lv_obj_remove_state(lbl, LV_STATE_USER_2);
            lv_obj_add_state(lbl,    LV_STATE_USER_1);
        } else if (val >= danger) {
            snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
            lv_obj_remove_state(lbl, LV_STATE_USER_2);
            lv_obj_add_state(lbl,    LV_STATE_USER_1);
        } else if (val >= warn) {
            snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
            lv_obj_remove_state(lbl, LV_STATE_USER_1);
            lv_obj_add_state(lbl,    LV_STATE_USER_2);
        } else {
            snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
            lv_obj_remove_state(lbl, LV_STATE_USER_1);
            lv_obj_remove_state(lbl, LV_STATE_USER_2);
        }
        lv_label_set_text(lbl, b);
    };

    // Extended temp sensor helper – offline shows muted "---" (no alarm colour)
    auto setExtLabel = [&](lv_obj_t* lbl, float val,
                           float warn, float danger) {
        if (!lbl) return;
        char b[24];
        if (val <= TEMP_OFFLINE_THRESH) {
            snprintf(b, sizeof(b), "---");
            lv_obj_remove_state(lbl, LV_STATE_USER_1);
            lv_obj_remove_state(lbl, LV_STATE_USER_2);
            lv_obj_set_style_text_color(lbl, CLR_MUTED, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            // Restore normal colour override (may have been set muted)
            lv_obj_set_style_text_color(lbl, CLR_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);
            if (val >= danger) {
                snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
                lv_obj_remove_state(lbl, LV_STATE_USER_2);
                lv_obj_add_state(lbl,    LV_STATE_USER_1);
            } else if (val >= warn) {
                snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
                lv_obj_remove_state(lbl, LV_STATE_USER_1);
                lv_obj_add_state(lbl,    LV_STATE_USER_2);
            } else {
                snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
                lv_obj_remove_state(lbl, LV_STATE_USER_1);
                lv_obj_remove_state(lbl, LV_STATE_USER_2);
            }
        }
        lv_label_set_text(lbl, b);
    };

    // Extended flow helper – offline = muted "---"
    auto setExtFlowLabel = [&](lv_obj_t* lbl, float val) {
        if (!lbl) return;
        char b[24];
        if (val <= SENSOR_OFFLINE + 1.0f) {
            snprintf(b, sizeof(b), "---");
            lv_obj_remove_state(lbl, LV_STATE_USER_1);
            lv_obj_set_style_text_color(lbl, CLR_MUTED, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_text_color(lbl, CLR_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);
            snprintf(b, sizeof(b), "%.2f L/m", val);
            lv_obj_remove_state(lbl, LV_STATE_USER_1);
        }
        lv_label_set_text(lbl, b);
    };

    // Use safetyTempMaxC as fallback warn/danger for extended sensors that
    // have no individual threshold configured.
    float extDanger = s.safetyTempMaxC;
    float extWarn   = extDanger * 0.92f;

    // LEFT COLUMN – temperatures
    setThreshLabel(mon_t1Lbl, s.roomTemp,    thr.tempWarn[0], thr.tempDanger[0]);
    setThreshLabel(mon_t2Lbl, s.kettleTemp,  thr.tempWarn[1], thr.tempDanger[1]);
    setThreshLabel(mon_t3Lbl, s.pillar1Temp, thr.tempWarn[2], thr.tempDanger[2]);
    setExtLabel(mon_pillar2Lbl,  s.pillar2Temp,  extWarn, extDanger);
    setExtLabel(mon_pillar3Lbl,  s.pillar3Temp,  extWarn, extDanger);
    setExtLabel(mon_dephlegmLbl, s.dephlegmTemp, extWarn, extDanger);
    setExtLabel(mon_refluxLbl,   s.refluxTemp,   extWarn, extDanger);

    // RIGHT COLUMN – pressure / level / flow
    if (mon_pLbl) {
        char b[24];
        if (s.pressureBar <= SENSOR_OFFLINE + 1.0f) {
            snprintf(b, sizeof(b), "%s", STR_OFFLINE);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_2);
            lv_obj_add_state(mon_pLbl,    LV_STATE_USER_1);
        } else if (s.pressureBar >= thr.pressDanger) {
            snprintf(b, sizeof(b), "%.1f kPa", s.pressureBar * BAR_TO_KPA);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_2);
            lv_obj_add_state(mon_pLbl,    LV_STATE_USER_1);
        } else if (s.pressureBar >= thr.pressWarn) {
            snprintf(b, sizeof(b), "%.1f kPa", s.pressureBar * BAR_TO_KPA);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_1);
            lv_obj_add_state(mon_pLbl,    LV_STATE_USER_2);
        } else {
            snprintf(b, sizeof(b), "%.1f kPa", s.pressureBar * BAR_TO_KPA);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_1);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_2);
        }
        lv_label_set_text(mon_pLbl, b);
    }

    if (mon_levelLbl) {
        lv_label_set_text(mon_levelLbl, s.levelHigh ? STR_LEVEL_OK : STR_LEVEL_LOW);
        lv_obj_set_style_text_color(mon_levelLbl,
            s.levelHigh ? CLR_GREEN : CLR_DANGER, 0);
    }

    if (mon_flowLbl) {
        char b[24];
        if (s.flowRateLPM <= SENSOR_OFFLINE + 1.0f) {
            snprintf(b, sizeof(b), "%s", STR_OFFLINE);
            lv_obj_add_state(mon_flowLbl, LV_STATE_USER_1);
        } else {
            snprintf(b, sizeof(b), "%.2f L/m", s.flowRateLPM);
            lv_obj_remove_state(mon_flowLbl, LV_STATE_USER_1);
        }
        lv_label_set_text(mon_flowLbl, b);
    }

    setExtFlowLabel(mon_waterDephlLbl, s.waterDephlLpm);
    setExtFlowLabel(mon_waterCondLbl,  s.waterCondLpm);
    setExtLabel(mon_productLbl, s.productTemp, extWarn, extDanger);

    // Total volume – header bar (show only once a run has started or volume > 0).
    // Use LV_OBJ_FLAG_HIDDEN rather than empty text so the flex container's
    // SPACE_EVENLY distribution ignores the slot entirely when idle.
    if (hdr_total) {
        if (s.isRunning || s.totalVolumeLiters > 0.0f) {
            char b[20];
            snprintf(b, sizeof(b), "%.3f L", s.totalVolumeLiters);
            lv_label_set_text(hdr_total, b);
            lv_obj_clear_flag(hdr_total, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(hdr_total, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // -----------------------------------------------------------------------
    // Monitor panel: Master Power slider + percentage label
    //   This is the same live control as the Control screen slider.
    //   Only update slider position if the user is not currently dragging
    //   (same guard logic as ctrl_masterSlider above).
    // -----------------------------------------------------------------------
    if (mon_loadSlider) {
        int32_t curVal   = lv_slider_get_value(mon_loadSlider);
        int32_t stateVal = (int32_t)s.masterPower;
        if (curVal != stateVal)
            lv_slider_set_value(mon_loadSlider, stateVal, LV_ANIM_OFF);
    }
    if (mon_loadPct) {
        char b[8];
        snprintf(b, sizeof(b), STR_MON_LOAD_FMT, s.masterPower);
        lv_label_set_text(mon_loadPct, b);
        lv_obj_set_style_text_color(mon_loadPct,
            s.masterPower > 0.0f ? CLR_ACCENT : CLR_MUTED, 0);
    }
}


// ===========================================================================
// Public screen-switch helpers  (called from LVGL code only)
// ===========================================================================
void uiShowModeScreen()    { showOnlyPanel(pnl_mode); }
void uiShowControlScreen() { showOnlyPanel(pnl_ctrl); }
void uiShowMonitorScreen() { showOnlyPanel(pnl_mon);  }
