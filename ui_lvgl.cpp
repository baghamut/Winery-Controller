// =============================================================================
//  ui_lvgl.cpp  –  LVGL v9 UI  (single screen, 4 panels, 480×320 landscape)
//  Source: https://raw.githubusercontent.com/baghamut/Winery-Controller/main/ui_lvgl.cpp
// =============================================================================
#include "ui_lvgl.h"
#include "config.h"
#include "state.h"
#include "control.h"
#include "sensors.h"
#include "ui_strings.h"
#include <lvgl.h>

extern const lv_img_dsc_t img_barrel;

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
#define MON_ROW_H     22
#define MON_ROW_Y0    24
#define MON_ROW_DY    22
#define MON_STOP_Y    (MON_ROW_Y0 + 8 * MON_ROW_DY + 6)

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define CLR_BG       lv_color_hex(0x111111)
#define CLR_CARD     lv_color_hex(0x202020)
#define CLR_CARD_ROW lv_color_hex(0x181818)
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
// Widget handles
// ---------------------------------------------------------------------------
static lv_obj_t* scr_root   = nullptr;
static lv_obj_t* hdr_bar    = nullptr;
static lv_obj_t* hdr_status = nullptr;
static lv_obj_t* hdr_t1     = nullptr;
static lv_obj_t* hdr_ip     = nullptr;
static lv_obj_t* hdr_tmax   = nullptr;
static lv_obj_t* hdr_ip_btn = nullptr;

static lv_obj_t* pnl_mode   = nullptr;
static lv_obj_t* pnl_ctrl   = nullptr;
static lv_obj_t* pnl_mon    = nullptr;
static lv_obj_t* pnl_wifi   = nullptr;
static lv_obj_t* pnl_tmax   = nullptr;

static lv_obj_t* btn_mode_dist = nullptr;
static lv_obj_t* btn_mode_rect = nullptr;

struct SsrRow {
    lv_obj_t* row;
    lv_obj_t* slider;
    lv_obj_t* sw;
    lv_obj_t* pct;
    uint8_t   ssrIndex;
};
static SsrRow    ctrl_rows[5];
static lv_obj_t* ctrl_title = nullptr;
static lv_obj_t* ctrl_start = nullptr;
static lv_obj_t* ctrl_back  = nullptr;

// Sensor max-temp buttons on Control panel
static lv_obj_t* btn_tmax_s[3];
static lv_obj_t* lbl_tmax_s[3];
static int       s_activeTmaxSensor = 1;
static lv_obj_t* tmax_ta    = nullptr;
static lv_obj_t* tmax_kb    = nullptr;

static lv_obj_t* mon_t1Lbl    = nullptr;
static lv_obj_t* mon_t2Lbl    = nullptr;
static lv_obj_t* mon_t3Lbl    = nullptr;
static lv_obj_t* mon_pLbl     = nullptr;
static lv_obj_t* mon_levelLbl = nullptr;
static lv_obj_t* mon_flowLbl  = nullptr;
static lv_obj_t* mon_totalLbl = nullptr;
static lv_obj_t* mon_ssrLbl   = nullptr;
static lv_obj_t* mon_stop     = nullptr;

static lv_obj_t* wifi_ssid_ta  = nullptr;
static lv_obj_t* wifi_pass_ta  = nullptr;
static lv_obj_t* wifi_kb       = nullptr;

static volatile bool s_refreshRequested = false;
static lv_timer_t*   s_refreshTimer     = nullptr;

// Forward declarations
static void uiShowMainFromWifi();
static void uiShowWifiConfig();
static void showOnlyPanel(lv_obj_t* show);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void resetPanelStyle(lv_obj_t* obj)
{
    lv_obj_set_style_pad_all(obj,       0, 0);
    lv_obj_set_style_pad_row(obj,       0, 0);
    lv_obj_set_style_pad_column(obj,    0, 0);
    lv_obj_set_style_border_width(obj,  0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj,  0, 0);
}

static lv_obj_t* makeCard(lv_obj_t* parent,
                          lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card,      CLR_CARD,   0);
    lv_obj_set_style_bg_opa(card,        LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card,  CLR_BORDER, 0);
    lv_obj_set_style_border_width(card,  1, 0);
    lv_obj_set_style_radius(card,        8, 0);
    lv_obj_set_style_pad_all(card,       8, 0);
    lv_obj_set_style_outline_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card,  0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

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
    lv_obj_set_style_bg_color(btn,      bgColor, 0);
    lv_obj_set_style_bg_opa(btn,        LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn,  0, 0);
    lv_obj_set_style_radius(btn,        10, 0);
    lv_obj_set_style_shadow_width(btn,  0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn,       4, 0);
    lv_obj_set_style_text_font(btn,     &lv_font_montserrat_16, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, txtColor, 0);
    lv_obj_center(lbl);

    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

// ---------------------------------------------------------------------------
// SSR callbacks
// ---------------------------------------------------------------------------
static void cbSsrSlider(lv_event_t* e)
{
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    int ssr      = (int)(intptr_t)lv_event_get_user_data(e);
    int32_t v    = lv_slider_get_value(sl);
    handleCommand("SSR:" + String(ssr) + ":PWR:" + String(v));
}

static void cbSsrToggle(lv_event_t* e)
{
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    int ssr      = (int)(intptr_t)lv_event_get_user_data(e);
    bool on      = lv_obj_has_state(sw, LV_STATE_CHECKED);
    handleCommand("SSR:" + String(ssr) + (on ? ":ON" : ":OFF"));
}

// ---------------------------------------------------------------------------
// buildSsrRow
// ---------------------------------------------------------------------------
static void buildSsrRow(lv_obj_t* card, int idx, lv_coord_t y)
{
    lv_obj_t* row = lv_obj_create(card);
    ctrl_rows[idx].row      = row;
    ctrl_rows[idx].ssrIndex = (uint8_t)idx;

    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, lv_pct(100), CTRL_ROW_H);
    lv_obj_set_style_bg_color(row,      CLR_CARD_ROW, 0);
    lv_obj_set_style_bg_opa(row,        LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row,        6, 0);
    lv_obj_set_style_border_width(row,  0, 0);
    lv_obj_set_style_pad_all(row,       0, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row,  0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    char name[8];
    snprintf(name, sizeof(name), "SSR%d", idx + 1);
    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, CLR_MUTED, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* slider = lv_slider_create(row);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_size(slider, 200, 12);
    lv_obj_set_pos(slider, 64, (CTRL_ROW_H - 12) / 2);
    lv_obj_set_style_bg_color(slider, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider,   LV_OPA_50,  LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, CLR_ACCENT, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(slider, cbSsrSlider, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)(idx + 1));

    lv_obj_t* pct = lv_label_create(row);
    lv_label_set_text(pct, "0%");
    lv_obj_set_style_text_color(pct, CLR_MUTED, 0);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, 0);
    lv_obj_align(pct, LV_ALIGN_LEFT_MID, 64 + 200 + 8, 0);

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw,  LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, CLR_ACCENT,  LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw,  LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(sw,  LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_add_event_cb(sw, cbSsrToggle, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)(idx + 1));

    ctrl_rows[idx].slider = slider;
    ctrl_rows[idx].sw     = sw;
    ctrl_rows[idx].pct    = pct;
}

// ---------------------------------------------------------------------------
// buildHeader
// ---------------------------------------------------------------------------
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

    // SPACE_EVENLY distributes all 4 items (Status / T1 / Max / IP) evenly
    lv_obj_set_layout(hdr_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr_bar,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr_bar, 0, 0);

    // Status
    hdr_status = lv_label_create(hdr_bar);
    lv_label_set_text(hdr_status, STR_STATUS_STOPPED);
    lv_obj_set_style_text_color(hdr_status, CLR_MUTED, 0);
    lv_obj_set_style_text_font(hdr_status,  &lv_font_montserrat_14, 0);
    lv_obj_set_width(hdr_status, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(hdr_status, LV_TEXT_ALIGN_CENTER, 0);

    // T1 (Room sensor) – USER_1 = offline (red)
    hdr_t1 = lv_label_create(hdr_bar);
    {
        char buf[20];
        snprintf(buf, sizeof(buf), "%s: --", STR_SENSOR_NAME1);
        lv_label_set_text(hdr_t1, buf);
    }
    lv_obj_set_style_text_color(hdr_t1, CLR_GREEN,  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hdr_t1, CLR_DANGER, LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_add_state(hdr_t1, LV_STATE_USER_1);   // boot: offline
    lv_obj_set_style_text_font(hdr_t1, &lv_font_montserrat_14, 0);
    lv_obj_set_width(hdr_t1, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(hdr_t1, LV_TEXT_ALIGN_CENTER, 0);

    // Max current temp (or safety message)
    hdr_tmax = lv_label_create(hdr_bar);
    lv_label_set_text(hdr_tmax, "Max: --");
    lv_obj_set_style_text_color(hdr_tmax, CLR_MUTED, 0);
    lv_obj_set_style_text_font(hdr_tmax,  &lv_font_montserrat_14, 0);
    lv_obj_set_width(hdr_tmax, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(hdr_tmax, LV_TEXT_ALIGN_CENTER, 0);

    // IP – transparent button so the whole area is tappable
    hdr_ip_btn = lv_btn_create(hdr_bar);
    lv_obj_set_width(hdr_ip_btn,  LV_SIZE_CONTENT);
    lv_obj_set_height(hdr_ip_btn, HDR_H + 8);
    lv_obj_set_style_bg_opa(hdr_ip_btn,      LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_ip_btn, 0, 0);
    lv_obj_set_style_outline_width(hdr_ip_btn, 0, 0);
    lv_obj_set_style_shadow_width(hdr_ip_btn,  0, 0);
    lv_obj_set_style_pad_all(hdr_ip_btn,      0, 0);
    lv_obj_clear_flag(hdr_ip_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hdr_ip_btn, cbIpClicked, LV_EVENT_CLICKED, nullptr);

    hdr_ip = lv_label_create(hdr_ip_btn);
    lv_label_set_text(hdr_ip, "0.0.0.0");
    lv_obj_set_style_text_color(hdr_ip, CLR_GREEN, 0);
    lv_obj_set_style_text_font(hdr_ip,  &lv_font_montserrat_14, 0);
    lv_obj_center(hdr_ip);
}

// ---------------------------------------------------------------------------
// buildModePanel
// ---------------------------------------------------------------------------
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

    const lv_coord_t header_gap    = 8;
    const lv_coord_t card_gap      = 4;
    const lv_coord_t top_region_h  = CONTENT_H * 45 / 100;
    const lv_coord_t top_card_h    = top_region_h - header_gap;
    const lv_coord_t top_card_y    = header_gap;
    const lv_coord_t bottom_card_y = top_card_y + top_card_h + card_gap;
    const lv_coord_t bottom_card_h = CONTENT_H - bottom_card_y;

    // Top card: App title + icon
    lv_obj_t* top_card = makeCard(pnl_mode, 8, top_card_y, CARD_OUTER_W, top_card_h);

    lv_obj_t* app_title = lv_label_create(top_card);
    lv_label_set_text(app_title, STR_APP_TITLE);
    lv_obj_set_style_text_font(app_title,  &lv_font_montserrat_22_bold, 0);
    lv_obj_set_style_text_color(app_title, CLR_ACCENT, 0);
    lv_obj_center(app_title);

    lv_obj_t* icon_left = lv_image_create(top_card);
    lv_image_set_src(icon_left, &img_barrel);
    lv_obj_align_to(icon_left, app_title, LV_ALIGN_OUT_LEFT_MID, -16, 0);

    // Bottom card: mode buttons
    lv_obj_t* card = makeCard(pnl_mode, 8, bottom_card_y, CARD_OUTER_W, bottom_card_h);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, STR_TITLE_MODE_SEL);
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_16, 0);
    lv_obj_set_pos(title, 0, 0);

    const lv_coord_t btnY = 32;
    const lv_coord_t btnH = bottom_card_h - btnY - 24;
    const lv_coord_t btnW = (CARD_INNER_W - 8) / 2;

    btn_mode_dist = makeBtn(card, STR_PROC_DIST, CLR_DIST_BG, CLR_ACCENT,
                            0,        btnY, btnW, btnH, cbModeDist);
    btn_mode_rect = makeBtn(card, STR_PROC_RECT, CLR_RECT_BG, CLR_ACCENT,
                            btnW + 8, btnY, btnW, btnH, cbModeRect);
}

// ---------------------------------------------------------------------------
// Max-temp config panel tunables
// ---------------------------------------------------------------------------
static const lv_coord_t TMAX_ROW_HEIGHT = 48;
static const lv_coord_t TMAX_BTN_WIDTH  = 60;
static const lv_coord_t TMAX_BTN_HEIGHT = 36;
static const lv_coord_t TMAX_BTN_RADIUS = 4;

// ---------------------------------------------------------------------------
// buildTmaxPanel
// ---------------------------------------------------------------------------
static void buildTmaxPanel()
{
    pnl_tmax = lv_obj_create(scr_root);
    lv_obj_set_pos(pnl_tmax, 0, CONTENT_Y);
    lv_obj_set_size(pnl_tmax, UI_W, CONTENT_H);
    lv_obj_set_style_bg_color(pnl_tmax, CLR_BG, 0);
    lv_obj_set_style_bg_opa(pnl_tmax, LV_OPA_COVER, 0);
    resetPanelStyle(pnl_tmax);
    lv_obj_clear_flag(pnl_tmax, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pnl_tmax, LV_OBJ_FLAG_HIDDEN);

    // Card leaves 140 px at bottom for the on-screen keyboard
    lv_obj_t* card = makeCard(pnl_tmax, 8, 4, CARD_OUTER_W, CONTENT_H - 140);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, STR_TMAX_PANEL_TITLE);
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(title, 0, 0);

    // [-5][-1][Text Area][+1][+5] row
    lv_obj_t* grp = lv_obj_create(card);
    lv_obj_set_size(grp, lv_pct(100), 50);
    lv_obj_set_pos(grp, 0, 30);
    lv_obj_set_style_bg_opa(grp,      LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grp, 0, 0);
    lv_obj_set_style_pad_all(grp,     0, 0);
    lv_obj_clear_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(grp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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
                float v = atof(cur) + d;
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", v);
                lv_textarea_set_text(tmax_ta, buf);
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)delta);
    };

    makeTmaxBtnInGrp("-5", -5);
    makeTmaxBtnInGrp("-1", -1);

    tmax_ta = lv_textarea_create(grp);
    lv_textarea_set_one_line(tmax_ta, true);
    lv_textarea_set_accepted_chars(tmax_ta, "0123456789.");
    lv_obj_set_size(tmax_ta, 80, 40);
    lv_obj_set_style_text_align(tmax_ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(tmax_ta,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_style_bg_color(tmax_ta,   CLR_CARD_ROW, 0);
    lv_obj_set_style_text_color(tmax_ta, CLR_TEXT, 0);
    lv_obj_set_style_border_width(tmax_ta, 1, 0);
    lv_obj_set_style_border_color(tmax_ta, CLR_ACCENT,
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(tmax_ta, [](lv_event_t* e) {
        if (tmax_kb) lv_keyboard_set_textarea(tmax_kb, tmax_ta);
    }, LV_EVENT_FOCUSED, nullptr);

    makeTmaxBtnInGrp("+1", 1);
    makeTmaxBtnInGrp("+5", 5);

    tmax_kb = lv_keyboard_create(pnl_tmax);
    lv_keyboard_set_mode(tmax_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(tmax_kb, UI_W, 130);
    lv_obj_align(tmax_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(tmax_kb, tmax_ta);

    const lv_coord_t gap   = 8;
    const lv_coord_t halfW = (CARD_INNER_W - gap) / 2;
    const lv_coord_t btnY  = 90;
    const lv_coord_t btnH  = 36;

    lv_obj_t* btn_back = makeBtn(card, STR_BTN_BACK, BCK_CARD, CLR_TEXT,
                                 0, btnY, halfW, btnH,
                                 [](lv_event_t*) { uiShowMainFromWifi(); });
    lv_obj_set_style_text_font(btn_back, &lv_font_montserrat_16_bold, 0);

    // Save: sends TMAX:N:SET:val – matches updated handleCommand() format
    lv_obj_t* btn_save = makeBtn(card, STR_BTN_SAVE, STR_GREEN, CLR_TEXT,
                                 halfW + gap, btnY, halfW, btnH, [](lv_event_t*) {
        if (!tmax_ta) return;
        const char* val = lv_textarea_get_text(tmax_ta);
        handleCommand("TMAX:" + String(s_activeTmaxSensor) + ":SET:" + String(val));
        uiRequestRefresh();
        uiShowMainFromWifi();
    });
    lv_obj_set_style_text_font(btn_save, &lv_font_montserrat_16_bold, 0);
}

// ---------------------------------------------------------------------------
// buildControlPanel
// ---------------------------------------------------------------------------
static void cbStart(lv_event_t*)       { handleCommand("START"); }
static void cbControlBack(lv_event_t*) { handleCommand("MODE:0"); }

static void cbOpenTmaxConfig(lv_event_t* e)
{
    s_activeTmaxSensor = (int)(intptr_t)lv_event_get_user_data(e);

    stateLock();
    float val = (g_state.processMode == 2)
                    ? g_state.threshRect.tempDanger[s_activeTmaxSensor - 1]
                    : g_state.threshDist.tempDanger[s_activeTmaxSensor - 1];
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

    ctrl_title = lv_label_create(card);
    lv_label_set_text(ctrl_title, STR_TITLE_CTRL);
    lv_obj_set_style_text_color(ctrl_title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(ctrl_title,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(ctrl_title, 0, 0);

    // SSR rows: 0-2 for Distillation, 3-4 for Rectification
    for (int i = 0; i < 3; ++i)
        buildSsrRow(card, i, CTRL_ROW_Y0 + i * CTRL_ROW_DY);
    for (int i = 3; i < 5; ++i)
        buildSsrRow(card, i, CTRL_ROW_Y0 + (i - 3) * CTRL_ROW_DY);

    // Sensor limits row (3 buttons, one per sensor)
    lv_coord_t rowY   = CTRL_ROW_Y0 + 3 * CTRL_ROW_DY;
    lv_obj_t*  tmaxRow = lv_obj_create(card);
    lv_obj_set_pos(tmaxRow, 0, rowY);
    lv_obj_set_size(tmaxRow, lv_pct(100), TMAX_ROW_HEIGHT);
    lv_obj_set_style_bg_opa(tmaxRow,       LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tmaxRow, 0, 0);
    lv_obj_set_style_pad_all(tmaxRow,      0, 0);
    lv_obj_clear_flag(tmaxRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(tmaxRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tmaxRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tmaxRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tmaxRow, 8, 0);

    lv_obj_t* lblSet = lv_label_create(tmaxRow);
    lv_label_set_text(lblSet, STR_LIMITS_LABEL);
    lv_obj_set_style_text_color(lblSet, CLR_MUTED, 0);
    lv_obj_set_style_text_font(lblSet,  &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(lblSet, 0);

    for (int i = 0; i < 3; i++) {
        btn_tmax_s[i] = lv_btn_create(tmaxRow);
        lv_obj_set_size(btn_tmax_s[i], 0, TMAX_ROW_HEIGHT - 4);
        lv_obj_set_flex_grow(btn_tmax_s[i], 1);
        lv_obj_set_style_pad_all(btn_tmax_s[i],  0, 0);
        lv_obj_set_style_pad_gap(btn_tmax_s[i],  0, 0);
        lv_obj_set_style_border_side(btn_tmax_s[i], LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_bg_color(btn_tmax_s[i],    lv_color_hex(0x383838), 0);
        lv_obj_set_style_bg_opa(btn_tmax_s[i],      LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn_tmax_s[i], 1, 0);
        lv_obj_set_style_border_color(btn_tmax_s[i], lv_color_hex(0x555555), 0);
        lv_obj_set_style_radius(btn_tmax_s[i],       4, 0);

        lbl_tmax_s[i] = lv_label_create(btn_tmax_s[i]);
        lv_label_set_text(lbl_tmax_s[i], "--");
        lv_label_set_long_mode(lbl_tmax_s[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl_tmax_s[i], lv_pct(100));
        lv_obj_set_style_text_color(lbl_tmax_s[i], CLR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_tmax_s[i],  &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(lbl_tmax_s[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl_tmax_s[i]);

        lv_obj_add_event_cb(btn_tmax_s[i], cbOpenTmaxConfig, LV_EVENT_CLICKED,
                            (void*)(intptr_t)(i + 1));
    }

    // Back + START
    const lv_coord_t gap     = 8;
    const lv_coord_t halfW   = (CARD_INNER_W - gap) / 2;
    const lv_coord_t btnH    = 36;
    const lv_coord_t marginB = 4;
    lv_coord_t startY = 8 + CARD_INNER_H - btnH - marginB - 2;

    ctrl_back = makeBtn(card, STR_BTN_BACK, BCK_CARD, CLR_TEXT,
                        0, startY, halfW, btnH, cbControlBack);
    lv_obj_set_style_text_font(ctrl_back, &lv_font_montserrat_16_bold, 0);

    ctrl_start = makeBtn(card, STR_BTN_START, STR_GREEN, CLR_TEXT,
                         halfW + gap, startY, halfW, btnH, cbStart);
    lv_obj_set_style_text_font(ctrl_start, &lv_font_montserrat_16_bold, 0);
}

// ---------------------------------------------------------------------------
// buildMonitorPanel
// ---------------------------------------------------------------------------
static void cbStop(lv_event_t*)
{
    handleCommand("STOP");
    handleCommand("MODE:0");
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

    lv_obj_t* titleLbl = lv_label_create(card);
    lv_label_set_text(titleLbl, STR_TITLE_MONITOR);
    lv_obj_set_style_text_color(titleLbl, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(titleLbl,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(titleLbl, 0, 0);

    auto makeRow = [&](const char* name, lv_coord_t y, lv_obj_t** outLbl) {
        lv_obj_t* row = lv_obj_create(card);
        lv_obj_set_pos(row, 0, y);
        lv_obj_set_size(row, lv_pct(100), MON_ROW_H);
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
        lv_obj_set_style_text_color(l2, CLR_TEXT, 0);
        lv_obj_set_style_text_font(l2,  &lv_font_montserrat_14, 0);
        lv_obj_align(l2, LV_ALIGN_RIGHT_MID, -4, 0);
        *outLbl = l2;
    };

    lv_coord_t y = MON_ROW_Y0;
    makeRow(STR_SENSOR_NAME1, y, &mon_t1Lbl);    y += MON_ROW_DY;
    makeRow(STR_SENSOR_NAME2, y, &mon_t2Lbl);    y += MON_ROW_DY;
    makeRow(STR_SENSOR_NAME3, y, &mon_t3Lbl);    y += MON_ROW_DY;
    makeRow(STR_MON_PRESSURE, y, &mon_pLbl);     y += MON_ROW_DY;
    makeRow(STR_MON_LEVEL,    y, &mon_levelLbl); y += MON_ROW_DY;
    makeRow(STR_MON_FLOW,     y, &mon_flowLbl);  y += MON_ROW_DY;
    makeRow(STR_MON_TOTAL,    y, &mon_totalLbl); y += MON_ROW_DY;
    makeRow(STR_MON_SSRS,     y, &mon_ssrLbl);   y += MON_ROW_DY;
    lv_label_set_recolor(mon_ssrLbl, true);

    // Offline (USER_1) and threshold-warn (USER_2) colors
    lv_obj_t* offlineLbls[] = { mon_t1Lbl, mon_t2Lbl, mon_t3Lbl, mon_pLbl, mon_flowLbl };
    for (auto* lbl : offlineLbls) {
        lv_obj_set_style_text_color(lbl, CLR_DANGER, LV_PART_MAIN | LV_STATE_USER_1);
        lv_obj_add_state(lbl, LV_STATE_USER_1);
    }
    lv_obj_t* threshLbls[] = { mon_t1Lbl, mon_t2Lbl, mon_t3Lbl, mon_pLbl };
    for (auto* lbl : threshLbls)
        lv_obj_set_style_text_color(lbl, CLR_ACCENT, LV_PART_MAIN | LV_STATE_USER_2);

    mon_stop = makeBtn(card, STR_BTN_STOP, CLR_DANGER, CLR_TEXT,
                       0, MON_STOP_Y, lv_pct(100), 32, cbStop);
}

// ---------------------------------------------------------------------------
// buildWifiPanel
// ---------------------------------------------------------------------------
static void cbWifiBack(lv_event_t*) { uiShowMainFromWifi(); }

static void cbWifiSave(lv_event_t*)
{
    if (!wifi_ssid_ta || !wifi_pass_ta) return;
    const char* ssid = lv_textarea_get_text(wifi_ssid_ta);
    const char* pass = lv_textarea_get_text(wifi_pass_ta);
    wifiApplyConfig(ssid, pass);
    uiShowMainFromWifi();
}

static void cbWifiTaFocus(lv_event_t* e)
{
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
    lv_obj_add_flag(pnl_wifi, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* card = makeCard(pnl_wifi, 8, 4, CARD_OUTER_W, CONTENT_H - 8);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, STR_TITLE_WIFI_SETUP);
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_16_bold, 0);
    lv_obj_set_pos(title, 0, 0);

    lv_obj_t* lblSsid = lv_label_create(card);
    lv_label_set_text(lblSsid, STR_WIFI_SSID_LABEL);
    lv_obj_set_style_text_color(lblSsid, CLR_MUTED, 0);
    lv_obj_set_style_text_font(lblSsid,  &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lblSsid, 0, 22);

    wifi_ssid_ta = lv_textarea_create(card);
    lv_textarea_set_one_line(wifi_ssid_ta, true);
    lv_textarea_set_max_length(wifi_ssid_ta, 32);
    lv_obj_set_size(wifi_ssid_ta, lv_pct(100), 30);
    lv_obj_set_pos(wifi_ssid_ta, 0, 36);
    lv_obj_set_style_bg_color(wifi_ssid_ta,   CLR_CARD_ROW, 0);
    lv_obj_set_style_text_color(wifi_ssid_ta, CLR_TEXT, 0);
    lv_textarea_set_text(wifi_ssid_ta, wifiGetSsid() ? wifiGetSsid() : "");
    lv_obj_add_event_cb(wifi_ssid_ta, cbWifiTaFocus, LV_EVENT_FOCUSED, nullptr);

    lv_obj_t* lblPass = lv_label_create(card);
    lv_label_set_text(lblPass, STR_WIFI_PASS_LABEL);
    lv_obj_set_style_text_color(lblPass, CLR_MUTED, 0);
    lv_obj_set_style_text_font(lblPass,  &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lblPass, 0, 72);

    wifi_pass_ta = lv_textarea_create(card);
    lv_textarea_set_one_line(wifi_pass_ta, true);
    lv_textarea_set_password_mode(wifi_pass_ta, true);
    lv_textarea_set_max_length(wifi_pass_ta, 64);
    lv_obj_set_size(wifi_pass_ta, lv_pct(100), 30);
    lv_obj_set_pos(wifi_pass_ta, 0, 86);
    lv_obj_set_style_bg_color(wifi_pass_ta,   CLR_CARD_ROW, 0);
    lv_obj_set_style_text_color(wifi_pass_ta, CLR_TEXT, 0);
    lv_textarea_set_text(wifi_pass_ta, wifiGetPass() ? wifiGetPass() : "");
    lv_obj_add_event_cb(wifi_pass_ta, cbWifiTaFocus, LV_EVENT_FOCUSED, nullptr);

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

// ---------------------------------------------------------------------------
// Panel navigation helpers
// ---------------------------------------------------------------------------
static void showOnlyPanel(lv_obj_t* show)
{
    lv_obj_t* panels[] = { pnl_mode, pnl_ctrl, pnl_mon, pnl_wifi, pnl_tmax };
    for (auto* p : panels) {
        if (!p) continue;
        if (p == show) lv_obj_clear_flag(p, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(p,   LV_OBJ_FLAG_HIDDEN);
    }
}

static void uiShowMainFromWifi()
{
    stateLock();
    int  pm      = g_state.processMode;
    bool running = g_state.isRunning;
    stateUnlock();

    if (pm == 0)          showOnlyPanel(pnl_mode);
    else if (!running)    showOnlyPanel(pnl_ctrl);
    else                  showOnlyPanel(pnl_mon);
}

static void uiShowWifiConfig()
{
    showOnlyPanel(pnl_wifi);
}

// ---------------------------------------------------------------------------
// uiInit
// ---------------------------------------------------------------------------
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

    showOnlyPanel(pnl_mode);

    s_refreshTimer = lv_timer_create([](lv_timer_t*) {
        if (s_refreshRequested) {
            s_refreshRequested = false;
            uiRefreshFromState();
        }
    }, LVGL_STATE_REFRESH_MS, nullptr);
}

// ---------------------------------------------------------------------------
// uiRequestRefresh
// ---------------------------------------------------------------------------
void uiRequestRefresh()
{
    s_refreshRequested = true;
}

// ---------------------------------------------------------------------------
// uiRefreshFromState
// ---------------------------------------------------------------------------
void uiRefreshFromState()
{
    stateLock();
    AppState s = g_state;
    stateUnlock();

    // Status badge
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

    // Header T1 (Room sensor)
    {
        char tb[32];
        if (s.t1 <= TEMP_OFFLINE_THRESH) {
            snprintf(tb, sizeof(tb), "%s: --", STR_SENSOR_NAME1);
            lv_obj_add_state(hdr_t1, LV_STATE_USER_1);
        } else {
            snprintf(tb, sizeof(tb), STR_HDR_T1_FMT, STR_SENSOR_NAME1, s.t1);
            lv_obj_remove_state(hdr_t1, LV_STATE_USER_1);
        }
        lv_label_set_text(hdr_t1, tb);
    }

    // Header IP
    if (hdr_ip) {
        const char* ipStr = s.ip.c_str();
        bool bad = wifiIpLooksBad(ipStr);
        lv_label_set_text(hdr_ip, ipStr && ipStr[0] ? ipStr : "0.0.0.0");
        lv_obj_set_style_text_color(hdr_ip, bad ? CLR_DANGER : CLR_GREEN, 0);
    }

    // Header Max – highest current sensor reading (or safety message)
    if (hdr_tmax) {
        if (s.safetyTripped && s.safetyMessage.length() > 0) {
            lv_label_set_text(hdr_tmax, s.safetyMessage.c_str());
            lv_obj_set_style_text_color(hdr_tmax, CLR_DANGER, 0);
        } else {
            float maxTemp = TEMP_OFFLINE_THRESH;
            if (s.t1 > TEMP_OFFLINE_THRESH) maxTemp = s.t1;
            if (s.t2 > TEMP_OFFLINE_THRESH && s.t2 > maxTemp) maxTemp = s.t2;
            if (s.t3 > TEMP_OFFLINE_THRESH && s.t3 > maxTemp) maxTemp = s.t3;

            char tb[24];
            if (maxTemp <= TEMP_OFFLINE_THRESH) {
                snprintf(tb, sizeof(tb), "Max: --");
            } else {
                snprintf(tb, sizeof(tb), "Max: %.1f%s", maxTemp, STR_UNIT_DEGC);
            }
            lv_label_set_text(hdr_tmax, tb);
            lv_obj_set_style_text_color(hdr_tmax, CLR_MUTED, 0);
        }
    }

    bool isMode    = (s.processMode == 0);
    bool isControl = (s.processMode != 0 && !s.isRunning);
    bool isMonitor = (s.processMode != 0 &&  s.isRunning);
    bool wifiVis   = pnl_wifi && !lv_obj_has_flag(pnl_wifi, LV_OBJ_FLAG_HIDDEN);
    bool tmaxVis   = pnl_tmax && !lv_obj_has_flag(pnl_tmax, LV_OBJ_FLAG_HIDDEN);

    if (!wifiVis && !tmaxVis) {
        if (isMode)         showOnlyPanel(pnl_mode);
        else if (isControl) showOnlyPanel(pnl_ctrl);
        else                showOnlyPanel(pnl_mon);
    }

    // Mode button highlights
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

    // Control panel title
    if (ctrl_title) {
        if      (s.processMode == 1) lv_label_set_text(ctrl_title, STR_TITLE_CTRL_DIST);
        else if (s.processMode == 2) lv_label_set_text(ctrl_title, STR_TITLE_CTRL_RECT);
        else                          lv_label_set_text(ctrl_title, STR_TITLE_CTRL);
    }

    // Sensor limit buttons – show current tempDanger for active mode
    if (lbl_tmax_s[0]) {
        const char* names[3] = { STR_SENSOR_NAME1, STR_SENSOR_NAME2, STR_SENSOR_NAME3 };
        for (int i = 0; i < 3; i++) {
            float val = (s.processMode == 2)
                          ? s.threshRect.tempDanger[i]
                          : s.threshDist.tempDanger[i];
            char buf[32];
            snprintf(buf, sizeof(buf), "%s: %.1f%s", names[i], val, STR_UNIT_DEGC);
            lv_label_set_text(lbl_tmax_s[i], buf);
        }
    }

    // SSR rows
    for (int i = 0; i < 5; ++i) {
        SsrRow& r = ctrl_rows[i];
        if (!r.row) continue;

        bool show = (s.processMode == 1 && i <= 2) ||
                    (s.processMode == 2 && i >= 3);
        if (!show) { lv_obj_add_flag(r.row, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(r.row, LV_OBJ_FLAG_HIDDEN);

        int idx = r.ssrIndex;
        if (r.slider) {
            float p = s.ssrPower[idx];
            lv_slider_set_value(r.slider, (int32_t)p, LV_ANIM_OFF);
            if (r.pct) {
                char pb[8];
                snprintf(pb, sizeof(pb), "%.0f%%", p);
                lv_label_set_text(r.pct, pb);
            }
        }
        if (r.sw) {
            if (s.ssrOn[idx]) lv_obj_add_state(r.sw, LV_STATE_CHECKED);
            else              lv_obj_clear_state(r.sw, LV_STATE_CHECKED);
        }
    }

    // Start button enable
    if (ctrl_start) {
        bool pmOk   = (s.processMode == 1 || s.processMode == 2);
        bool anySSR = false;
        for (int i = 0; i < 5; ++i)
            if (s.ssrOn[i] && s.ssrPower[i] > 0.0f) { anySSR = true; break; }
        bool canStart = pmOk && anySSR && !s.isRunning && !s.safetyTripped;
        if (canStart) lv_obj_clear_state(ctrl_start, LV_STATE_DISABLED);
        else          lv_obj_add_state(ctrl_start,   LV_STATE_DISABLED);
    }

    // Active threshold set
    const SensorThresholds& thr = (s.processMode == 2) ? s.threshRect : s.threshDist;

    auto setThreshLabel = [&](lv_obj_t* lbl, float val, float warn, float danger) {
        if (!lbl) return;
        char b[24];
        if (val <= TEMP_OFFLINE_THRESH) {
            snprintf(b, sizeof(b), "%s", STR_OFFLINE);
            lv_obj_remove_state(lbl, LV_STATE_USER_2);
            lv_obj_add_state   (lbl, LV_STATE_USER_1);
        } else if (val >= danger) {
            snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
            lv_obj_remove_state(lbl, LV_STATE_USER_2);
            lv_obj_add_state   (lbl, LV_STATE_USER_1);
        } else if (val >= warn) {
            snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
            lv_obj_remove_state(lbl, LV_STATE_USER_1);
            lv_obj_add_state   (lbl, LV_STATE_USER_2);
        } else {
            snprintf(b, sizeof(b), "%.1f%s", val, STR_UNIT_DEGC);
            lv_obj_remove_state(lbl, LV_STATE_USER_1);
            lv_obj_remove_state(lbl, LV_STATE_USER_2);
        }
        lv_label_set_text(lbl, b);
    };

    setThreshLabel(mon_t1Lbl, s.t1, thr.tempWarn[0], thr.tempDanger[0]);
    setThreshLabel(mon_t2Lbl, s.t2, thr.tempWarn[1], thr.tempDanger[1]);
    setThreshLabel(mon_t3Lbl, s.t3, thr.tempWarn[2], thr.tempDanger[2]);

    if (mon_pLbl) {
        char b[24];
        if (s.pressureBar <= -900.0f) {
            snprintf(b, sizeof(b), "%s", STR_OFFLINE);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_2);
            lv_obj_add_state   (mon_pLbl, LV_STATE_USER_1);
        } else if (s.pressureBar >= thr.pressDanger) {
            snprintf(b, sizeof(b), "%.2f bar", s.pressureBar);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_2);
            lv_obj_add_state   (mon_pLbl, LV_STATE_USER_1);
        } else if (s.pressureBar >= thr.pressWarn) {
            snprintf(b, sizeof(b), "%.2f bar", s.pressureBar);
            lv_obj_remove_state(mon_pLbl, LV_STATE_USER_1);
            lv_obj_add_state   (mon_pLbl, LV_STATE_USER_2);
        } else {
            snprintf(b, sizeof(b), "%.2f bar", s.pressureBar);
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
        if (s.flowRateLPM <= -900.0f) {
            snprintf(b, sizeof(b), "%s", STR_OFFLINE);
            lv_obj_add_state(mon_flowLbl, LV_STATE_USER_1);
        } else {
            snprintf(b, sizeof(b), "%.2f L/min", s.flowRateLPM);
            lv_obj_remove_state(mon_flowLbl, LV_STATE_USER_1);
        }
        lv_label_set_text(mon_flowLbl, b);
    }

    if (mon_totalLbl) {
        char b[24];
        snprintf(b, sizeof(b), "%.3f L", s.totalVolumeLiters);
        lv_label_set_text(mon_totalLbl, b);
        lv_obj_set_style_text_color(mon_totalLbl, CLR_TEXT, 0);
    }

    if (mon_ssrLbl) {
        char b[256] = "";
        for (int i = 0; i < 5; ++i) {
            bool relevant = (s.processMode == 1 && i <= 2) ||
                            (s.processMode == 2 && i >= 3);
            if (!relevant) continue;
            bool on = s.ssrOn[i];
            float p = s.ssrPower[i];
            char line[40];
            if (on) snprintf(line, sizeof(line), "#65FF7A S%d:ON %.0f%%# ", i+1, p);
            else    snprintf(line, sizeof(line), "#E02424 S%d:OFF# ",       i+1);
            strncat(b, line, sizeof(b) - strlen(b) - 1);
        }
        if (b[0] == '\0') {
            lv_label_set_text(mon_ssrLbl, "--");
            lv_obj_set_style_text_color(mon_ssrLbl, CLR_TEXT, 0);
        } else {
            lv_label_set_text(mon_ssrLbl, b);
        }
    }
}