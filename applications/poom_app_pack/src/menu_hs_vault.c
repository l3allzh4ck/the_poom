// SPDX-License-Identifier: MIT
// Copyright (c) 2026 THE POOM

// HS Vault (THE BEAST > HS VAULT): browse captured WPA handshakes on the SD
// card sorted by AP, review one, and push its hashcat .22000 to a cracking rig
// (the Hackberry Pi) over WiFi.
//
// Reads the .22000 sidecars poom_pcap writes under:
//   /sdcard/pcaps/handshakes/        -> auto   (POOMGOTCHI + PCAP sniffer), tag A
//   /sdcard/pcaps/handshakes/manual/ -> manual (deliberate captures),       tag M
// Each .22000 line is WPA*<type>*<hash>*<AP-BSSID>*<STA>*<ESSID-hex>*...  so the
// AP (BSSID + SSID) is parsed straight from the sidecar; no pcap parsing needed.
//
// Task/button/keyboard/HTTP scaffolding mirrors menu_conntest.c. All on-screen
// text is clipped to HV_COLS by hv_puts_ so nothing wraps on the 128x64 OLED.
//
// Sending moves a capture the operator already made to the operator's own rig.
// It performs no RF transmission, so (unlike the deauth demo) it needs no
// active-test authorization gate. Own gear / consented CTF only.

#include "menu_hs_vault.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Arduboy2.h"
#include "button_driver.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "poom_sbus.h"
#include "poom_secrets_store.h"
#include "poom_ui_keyboard.h"
#include "poom_wifi_ctrl.h"
#include "poom_wifi_profile_store.h"
#include "sd_card.h"

#define POOM_MENU_RESUME_TOPIC "poom/menu/resume"

#ifndef BTN_A
#define BTN_A (0U)
#endif
#ifndef BTN_B
#define BTN_B (1U)
#endif
#ifndef BTN_LEFT
#define BTN_LEFT (2U)
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT (3U)
#endif
#ifndef BTN_UP
#define BTN_UP (4U)
#endif
#ifndef BTN_DOWN
#define BTN_DOWN (5U)
#endif
#ifndef BUTTON_SINGLE_CLICK
#define BUTTON_SINGLE_CLICK (4U)
#endif

#define HV_COLS               (20)
#define HV_VISIBLE_ROWS       (4U)
#define HV_REFRESH_MS         (250U)
#define HV_STACK              (8192U) /* room for esp_http_client */
#define HV_PRIO               (4U)
#define HV_CONNECT_TIMEOUT_MS (20000U)
#define HV_CONNECT_ATTEMPTS   (3U)
#define HV_HTTP_TIMEOUT_MS    (15000)
#define HV_INFO_HOLD_MS       (1400U)

#define HV_MAX_ENTRIES        (64U)
#define HV_PATH_CAP           (160U)
#define HV_URL_CAP            (160U)
#define HV_SSID_CAP           POOM_WIFI_PROFILE_SSID_CAP /* 33 */

#define HV_DIR_AUTO   "/sdcard/pcaps/handshakes"
#define HV_DIR_MANUAL "/sdcard/pcaps/handshakes/manual"

/* Editable on-device (persisted in NVS); placeholder default until set. */
#define HV_DEF_URL  "http://192.168.1.50:8080/upload"
#define HV_NVS_KEY  "hs_pi_url"

typedef enum
{
    HV_LIST = 0,     /* handshakes sorted by AP */
    HV_DETAIL,       /* one handshake: AP / BSSID / type / size */
    HV_PICK_PROFILE, /* choose which saved WiFi to reach the Pi */
    HV_EDIT_URL,     /* keyboard: Pi endpoint URL */
    HV_CONNECTING,   /* associate + DHCP */
    HV_SENDING,      /* HTTP POST (blocking) */
    HV_RESULT,       /* send result */
    HV_INFO,         /* transient status, returns to list */
} hv_state_t;

typedef struct
{
    char path[HV_PATH_CAP];
    char ssid[HV_SSID_CAP];
    uint8_t bssid[6];
    bool has_ap;
    char type; /* 'P' PMKID, 'E' EAPOL, '?' unknown */
    char src;  /* 'A' auto, 'M' manual */
    uint32_t size;
} hv_entry_t;

typedef struct
{
    uint8_t button;
    uint8_t event;
    uint32_t ts_ms;
} hv_button_msg_t;

static bool s_active = false;
static bool s_buttons_subscribed = false;
static bool s_exit_requested = false;
static bool s_rescan_pending = false;
static bool s_connect_request_pending = false;
static bool s_send_pending = false;
static bool s_send_after_ip = false;
static TaskHandle_t s_task = NULL;
static portMUX_TYPE s_task_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_sbus_user[] = "menu_hs_vault";

static hv_state_t s_state = HV_LIST;

static hv_entry_t s_entries[HV_MAX_ENTRIES];
static uint16_t s_entry_count = 0U;
static uint16_t s_entry_sel = 0U;
static uint16_t s_entry_window = 0U;

static poom_wifi_profile_t s_profiles[POOM_WIFI_PROFILE_MAX];
static uint16_t s_profile_count = 0U;
static uint16_t s_profile_sel = 0U;
static uint16_t s_profile_window = 0U;

static char s_connect_ssid[HV_SSID_CAP] = {0};
static char s_connect_pass[POOM_WIFI_PROFILE_PASS_CAP] = {0};
static uint32_t s_connect_start_ms = 0U;
static uint8_t s_connect_attempt = 0U;

static char s_pi_url[HV_URL_CAP] = {0};
static char s_edit_url[HV_URL_CAP] = {0};
static poom_ui_keyboard_t s_keyboard;

static char s_status[24] = {0};
static uint32_t s_info_until_ms = 0U;

static bool s_res_ok = false;
static char s_res_l1[24] = {0};
static char s_res_l2[24] = {0};
static char s_res_l3[24] = {0};

static esp_err_t hv_exit_(void);
static void hv_task_(void *arg);
static void hv_button_cb_(const poom_sbus_msg_t *msg, void *user_ctx);
static void hv_wifi_evt_cb_(const poom_wifi_ctrl_evt_info_t *info, void *user_ctx);

/* ---- small helpers ------------------------------------------------------- */

static uint32_t hv_now_ms_(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void hv_wake_(void)
{
    portENTER_CRITICAL(&s_task_lock);
    if (s_task != NULL)
    {
        (void)xTaskNotifyGive(s_task);
    }
    portEXIT_CRITICAL(&s_task_lock);
}

static void hv_puts_(int16_t x, int16_t y, const char *s)
{
    char buf[HV_COLS + 1];

    (void)snprintf(buf, sizeof(buf), "%.*s", HV_COLS, (s != NULL) ? s : "");
    poom_arduboy_set_cursor(x, y);
    (void)poom_arduboy_print(buf);
}

static void hv_header_(const char *title)
{
    poom_arduboy_clear();
    poom_arduboy_set_text_size(1);
    poom_arduboy_set_cursor(16, 2);
    (void)poom_arduboy_print(title);
    poom_arduboy_fill_rect(0, 0, ARDUBOY_WIDTH, 11, INVERT);
}

static void hv_footer_(const char *left, const char *right)
{
    if (left != NULL)
    {
        poom_arduboy_set_cursor(0, 56);
        (void)poom_arduboy_print(left);
    }
    if (right != NULL)
    {
        poom_arduboy_set_cursor(84, 56);
        (void)poom_arduboy_print(right);
    }
}

static void hv_begin_info_(const char *text)
{
    (void)snprintf(s_status, sizeof(s_status), "%.*s", HV_COLS, (text != NULL) ? text : "");
    s_state = HV_INFO;
    s_info_until_ms = hv_now_ms_() + HV_INFO_HOLD_MS;
    if (s_info_until_ms == 0U)
    {
        s_info_until_ms = 1U;
    }
}

static bool hv_info_elapsed_(void)
{
    if (s_info_until_ms == 0U)
    {
        return false;
    }
    return ((int32_t)(hv_now_ms_() - s_info_until_ms) >= 0);
}

static const char *hv_url_tail_(void)
{
    size_t n = strlen(s_pi_url);
    return (n > (size_t)HV_COLS) ? &s_pi_url[n - (size_t)HV_COLS] : s_pi_url;
}

/* ---- .22000 parsing ------------------------------------------------------ */

static int hv_hexval_(char c)
{
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'a') && (c <= 'f')) { return (c - 'a') + 10; }
    if ((c >= 'A') && (c <= 'F')) { return (c - 'A') + 10; }
    return -1;
}

/* Copy the n-th '*'-separated field of line into out (trims trailing CR/LF). */
static bool hv_field_(const char *line, int idx, char *out, size_t cap)
{
    const char *s = line;
    const char *e;
    size_t n;
    int f = 0;

    if ((line == NULL) || (out == NULL) || (cap == 0U))
    {
        return false;
    }
    while (f < idx)
    {
        s = strchr(s, '*');
        if (s == NULL)
        {
            return false;
        }
        s++;
        f++;
    }
    e = strchr(s, '*');
    n = (e != NULL) ? (size_t)(e - s) : strlen(s);
    while ((n > 0U) && ((s[n - 1U] == '\n') || (s[n - 1U] == '\r')))
    {
        n--;
    }
    if (n >= cap)
    {
        n = cap - 1U;
    }
    (void)memcpy(out, s, n);
    out[n] = '\0';
    return true;
}

static bool hv_hex12_to_mac_(const char *hex, uint8_t mac[6])
{
    if (strlen(hex) < 12U)
    {
        return false;
    }
    for (int i = 0; i < 6; i++)
    {
        int hi = hv_hexval_(hex[2 * i]);
        int lo = hv_hexval_(hex[(2 * i) + 1]);
        if ((hi < 0) || (lo < 0))
        {
            return false;
        }
        mac[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void hv_hex_to_ascii_(const char *hex, char *out, size_t cap)
{
    size_t oi = 0U;

    if (cap == 0U)
    {
        return;
    }
    for (size_t i = 0U; (hex[i] != '\0') && (hex[i + 1U] != '\0') && ((oi + 1U) < cap); i += 2U)
    {
        int hi = hv_hexval_(hex[i]);
        int lo = hv_hexval_(hex[i + 1U]);
        unsigned char c;

        if ((hi < 0) || (lo < 0))
        {
            break;
        }
        c = (unsigned char)((hi << 4) | lo);
        out[oi++] = ((c >= 0x20U) && (c < 0x7FU)) ? (char)c : '.';
    }
    out[oi] = '\0';
}

/* Parse the first WPA* line of a .22000 for AP BSSID, ESSID and type. */
static bool hv_parse_file_(const char *path, hv_entry_t *e)
{
    FILE *f = fopen(path, "r");
    char line[512];
    char t[4];
    char apx[16];
    char essx[80];
    bool ok = false;

    if (f == NULL)
    {
        return false;
    }
    if (fgets(line, sizeof(line), f) != NULL)
    {
        if ((strncmp(line, "WPA*", 4) == 0) && hv_field_(line, 1, t, sizeof(t)))
        {
            e->type = (strcmp(t, "01") == 0) ? 'P' : ((strcmp(t, "02") == 0) ? 'E' : '?');
            if (hv_field_(line, 3, apx, sizeof(apx)) && hv_hex12_to_mac_(apx, e->bssid))
            {
                e->has_ap = true;
            }
            if (hv_field_(line, 5, essx, sizeof(essx)))
            {
                hv_hex_to_ascii_(essx, e->ssid, sizeof(e->ssid));
            }
            ok = true;
        }
    }
    (void)fclose(f);
    return ok;
}

/* ---- directory scan ------------------------------------------------------ */

static void hv_scan_dir_(const char *dir, char src)
{
    DIR *d = opendir(dir);
    struct dirent *de;

    if (d == NULL)
    {
        return;
    }
    while (((de = readdir(d)) != NULL) && (s_entry_count < HV_MAX_ENTRIES))
    {
        const char *nm = de->d_name;
        size_t ln = strlen(nm);
        hv_entry_t *e;
        struct stat st;

        if ((ln < 7U) || (strcmp(&nm[ln - 6U], ".22000") != 0))
        {
            continue; /* skip ".", "..", the manual/ subdir, and non-.22000 */
        }
        e = &s_entries[s_entry_count];
        (void)memset(e, 0, sizeof(*e));
        e->src = src;
        e->type = '?';
        (void)snprintf(e->path, sizeof(e->path), "%s/%.*s", dir, 120, nm);
        (void)hv_parse_file_(e->path, e);
        if (e->ssid[0] == '\0')
        {
            (void)snprintf(e->ssid, sizeof(e->ssid), "<unknown>");
        }
        if (stat(e->path, &st) == 0)
        {
            e->size = (uint32_t)st.st_size;
        }
        s_entry_count++;
    }
    (void)closedir(d);
}

static int hv_cmp_(const void *a, const void *b)
{
    const hv_entry_t *x = (const hv_entry_t *)a;
    const hv_entry_t *y = (const hv_entry_t *)b;
    int c = strcasecmp(x->ssid, y->ssid);

    if (c != 0)
    {
        return c;
    }
    return strcmp(x->path, y->path);
}

static void hv_rescan_(void)
{
    s_entry_count = 0U;
    if (sd_card_is_not_mounted())
    {
        (void)sd_card_mount();
    }
    hv_scan_dir_(HV_DIR_AUTO, 'A');
    hv_scan_dir_(HV_DIR_MANUAL, 'M');
    if (s_entry_count > 1U)
    {
        qsort(s_entries, s_entry_count, sizeof(s_entries[0]), hv_cmp_);
    }
    s_entry_sel = 0U;
    s_entry_window = 0U;
}

/* ---- networking ---------------------------------------------------------- */

static esp_err_t hv_http_post_(const char *url, const uint8_t *body, size_t len,
                               int *status_out, char *err_out, size_t err_cap)
{
    esp_http_client_config_t cfg;
    esp_http_client_handle_t client;
    esp_err_t err;

    if (err_out != NULL)
    {
        err_out[0] = '\0';
    }

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = HV_HTTP_TIMEOUT_MS;
    if (strncmp(url, "https", 5) == 0)
    {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }

    client = esp_http_client_init(&cfg);
    if (client == NULL)
    {
        if (err_out != NULL)
        {
            (void)snprintf(err_out, err_cap, "init failed");
        }
        return ESP_FAIL;
    }

    (void)esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    (void)esp_http_client_set_post_field(client, (const char *)body, (int)len);

    err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        if (status_out != NULL)
        {
            *status_out = esp_http_client_get_status_code(client);
        }
    }
    else if (err_out != NULL)
    {
        (void)snprintf(err_out, err_cap, "%.*s", (int)(err_cap - 1U), esp_err_to_name(err));
    }

    (void)esp_http_client_cleanup(client);
    return err;
}

/* Read the selected capture and POST it. Blocking; fills s_res_*. */
static void hv_send_selected_(void)
{
    hv_entry_t *e = &s_entries[s_entry_sel];
    uint8_t *buf = NULL;
    size_t sz = 0U;
    const char *base;
    char url[HV_URL_CAP + 128];
    int status = 0;
    char errbuf[24];
    esp_err_t err;

    s_res_ok = false;
    s_res_l1[0] = '\0';
    s_res_l2[0] = '\0';
    s_res_l3[0] = '\0';

    if ((sd_card_read_file_to_buffer(e->path, &buf, &sz) != ESP_OK) || (buf == NULL))
    {
        (void)snprintf(s_res_l1, sizeof(s_res_l1), "READ FAIL");
        (void)snprintf(s_res_l2, sizeof(s_res_l2), "SD read error");
        s_state = HV_RESULT;
        return;
    }

    base = strrchr(e->path, '/');
    base = (base != NULL) ? (base + 1) : e->path;
    (void)snprintf(url, sizeof(url), "%s?name=%.*s", s_pi_url, 100, base);
    printf("[HV] POST %s (%u bytes)\n", url, (unsigned)sz);

    err = hv_http_post_(url, buf, sz, &status, errbuf, sizeof(errbuf));
    free(buf);
    printf("[HV] http rc=%s status=%d\n", esp_err_to_name(err), status);

    if (err != ESP_OK)
    {
        (void)snprintf(s_res_l1, sizeof(s_res_l1), "SEND FAIL");
        (void)snprintf(s_res_l2, sizeof(s_res_l2), "%.*s", HV_COLS, errbuf);
    }
    else
    {
        s_res_ok = ((status >= 200) && (status < 300));
        (void)snprintf(s_res_l1, sizeof(s_res_l1), "HTTP %d", status);
        (void)snprintf(s_res_l2, sizeof(s_res_l2), "%s", s_res_ok ? "SENT OK" : "PI REJECTED");
    }
    (void)snprintf(s_res_l3, sizeof(s_res_l3), "%.*s", HV_COLS, base);
    s_state = HV_RESULT;
}

static void hv_request_connect_(const char *ssid, const char *pass)
{
    (void)snprintf(s_connect_ssid, sizeof(s_connect_ssid), "%.*s",
                   (int)(HV_SSID_CAP - 1U), (ssid != NULL) ? ssid : "");
    (void)snprintf(s_connect_pass, sizeof(s_connect_pass), "%.*s",
                   (int)(POOM_WIFI_PROFILE_PASS_CAP - 1U), (pass != NULL) ? pass : "");
    s_connect_request_pending = true;
    s_send_after_ip = true;
    s_connect_attempt = 1U;
    s_state = HV_CONNECTING;
}

/* Decide how to reach the Pi and begin sending (called on A in DETAIL). */
static void hv_begin_send_(void)
{
    if (poom_wifi_ctrl_sta_has_ip())
    {
        s_send_after_ip = false;
        s_state = HV_SENDING;
        s_send_pending = true;
        return;
    }
    if (s_profile_count == 0U)
    {
        hv_begin_info_("No WiFi profile");
        return;
    }
    if (s_profile_count == 1U)
    {
        hv_request_connect_(s_profiles[0].ssid, s_profiles[0].pass);
        return;
    }
    s_profile_sel = 0U;
    s_profile_window = 0U;
    s_state = HV_PICK_PROFILE;
}

static void hv_enter_edit_url_(void)
{
    (void)snprintf(s_edit_url, sizeof(s_edit_url), "%.*s", (int)(HV_URL_CAP - 1U), s_pi_url);
    poom_ui_keyboard_init(&s_keyboard, s_edit_url, sizeof(s_edit_url), "PI URL");
    s_state = HV_EDIT_URL;
}

static void hv_update_connect_(void)
{
    if (poom_wifi_ctrl_sta_has_ip())
    {
        if (s_send_after_ip)
        {
            s_send_after_ip = false;
            s_state = HV_SENDING;
            s_send_pending = true;
            printf("[HV] got IP -> sending\n");
        }
        return;
    }
    if ((hv_now_ms_() - s_connect_start_ms) >= HV_CONNECT_TIMEOUT_MS)
    {
        (void)poom_wifi_ctrl_sta_disconnect();
        printf("[HV] join timeout (attempt %u/%u)\n",
               (unsigned)s_connect_attempt, (unsigned)HV_CONNECT_ATTEMPTS);
        if (s_connect_attempt < HV_CONNECT_ATTEMPTS)
        {
            s_connect_attempt++;              /* bounded auto-retry of the join */
            s_connect_request_pending = true; /* task re-issues sta_connect */
        }
        else
        {
            s_send_after_ip = false;
            hv_begin_info_("Connect timeout x3");
        }
    }
}

/* ---- drawing ------------------------------------------------------------- */

static void hv_draw_list_(void)
{
    hv_header_("HS VAULT");

    if (s_entry_count == 0U)
    {
        hv_puts_(10, 26, "No handshakes");
        hv_puts_(4, 38, "(pcaps/handshakes)");
        hv_footer_("A:RESCAN", "B:EXIT");
        poom_arduboy_display();
        return;
    }

    if (s_entry_sel < s_entry_window)
    {
        s_entry_window = s_entry_sel;
    }
    if (s_entry_sel >= (uint16_t)(s_entry_window + HV_VISIBLE_ROWS))
    {
        s_entry_window = (uint16_t)(s_entry_sel - HV_VISIBLE_ROWS + 1U);
    }

    for (uint16_t row = 0U; row < HV_VISIBLE_ROWS; row++)
    {
        uint16_t idx = (uint16_t)(s_entry_window + row);
        int16_t y;
        char line[40];

        if (idx >= s_entry_count)
        {
            break;
        }
        y = (int16_t)(14 + (int16_t)row * 10);
        (void)snprintf(line, sizeof(line), "%c %.*s", s_entries[idx].src,
                       HV_COLS - 2, s_entries[idx].ssid);
        hv_puts_(2, y, line);
        if (idx == s_entry_sel)
        {
            poom_arduboy_fill_rect(0, (int16_t)(y - 1), ARDUBOY_WIDTH, 9, INVERT);
        }
    }

    hv_footer_("A:OPEN", "B:EXIT");
    poom_arduboy_display();
}

static void hv_draw_detail_(void)
{
    hv_entry_t *e = &s_entries[s_entry_sel];
    char line[40];

    hv_header_("HANDSHAKE");

    (void)snprintf(line, sizeof(line), "%.*s", HV_COLS, e->ssid);
    hv_puts_(2, 14, line);

    if (e->has_ap)
    {
        (void)snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
                       e->bssid[0], e->bssid[1], e->bssid[2],
                       e->bssid[3], e->bssid[4], e->bssid[5]);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "BSSID: --");
    }
    hv_puts_(2, 24, line);

    (void)snprintf(line, sizeof(line), "%s  %s  %uB",
                   (e->type == 'P') ? "PMKID" : ((e->type == 'E') ? "EAPOL" : "?"),
                   (e->src == 'M') ? "MANUAL" : "AUTO",
                   (unsigned)e->size);
    hv_puts_(2, 34, line);

    (void)snprintf(line, sizeof(line), ">%.*s", HV_COLS - 1, hv_url_tail_());
    hv_puts_(2, 44, line);

    hv_footer_("A:SEND R:URL", "B:BACK");
    poom_arduboy_display();
}

static void hv_draw_pick_profile_(void)
{
    hv_header_("SEND VIA");

    if (s_profile_sel < s_profile_window)
    {
        s_profile_window = s_profile_sel;
    }
    if (s_profile_sel >= (uint16_t)(s_profile_window + HV_VISIBLE_ROWS))
    {
        s_profile_window = (uint16_t)(s_profile_sel - HV_VISIBLE_ROWS + 1U);
    }

    for (uint16_t row = 0U; row < HV_VISIBLE_ROWS; row++)
    {
        uint16_t idx = (uint16_t)(s_profile_window + row);
        int16_t y;

        if (idx >= s_profile_count)
        {
            break;
        }
        y = (int16_t)(14 + (int16_t)row * 10);
        hv_puts_(2, y, s_profiles[idx].ssid);
        if (idx == s_profile_sel)
        {
            poom_arduboy_fill_rect(0, (int16_t)(y - 1), ARDUBOY_WIDTH, 9, INVERT);
        }
    }

    hv_footer_("A:USE", "B:BACK");
    poom_arduboy_display();
}

static void hv_draw_result_(void)
{
    hv_header_(s_res_ok ? "SENT" : "FAILED");
    hv_puts_(4, 18, s_res_l1);
    hv_puts_(4, 30, s_res_l2);
    hv_puts_(4, 42, s_res_l3);
    hv_footer_("A:OK", "B:LIST");
    poom_arduboy_display();
}

static void hv_draw_(void)
{
    char line[40];

    switch (s_state)
    {
        case HV_LIST:
            hv_draw_list_();
            return;

        case HV_DETAIL:
            hv_draw_detail_();
            return;

        case HV_PICK_PROFILE:
            hv_draw_pick_profile_();
            return;

        case HV_EDIT_URL:
            poom_ui_keyboard_draw(&s_keyboard);
            return;

        case HV_CONNECTING:
            hv_header_("CONNECTING");
            (void)snprintf(line, sizeof(line), "%.*s", HV_COLS, s_connect_ssid);
            hv_puts_(4, 24, line);
            (void)snprintf(line, sizeof(line), "Attempt %u/%u",
                           (unsigned)s_connect_attempt, (unsigned)HV_CONNECT_ATTEMPTS);
            hv_puts_(4, 36, line);
            hv_puts_(22, 48, "Please wait...");
            hv_footer_(NULL, "B:CANCEL");
            poom_arduboy_display();
            return;

        case HV_SENDING:
            hv_header_("UPLOADING");
            (void)snprintf(line, sizeof(line), "%.*s", HV_COLS, s_entries[s_entry_sel].ssid);
            hv_puts_(4, 22, line);
            (void)snprintf(line, sizeof(line), ">%.*s", HV_COLS - 1, hv_url_tail_());
            hv_puts_(4, 34, line);
            hv_puts_(22, 46, "Please wait...");
            poom_arduboy_display();
            return;

        case HV_RESULT:
            hv_draw_result_();
            return;

        case HV_INFO:
        default:
            hv_header_("HS VAULT");
            hv_puts_(6, 30, s_status);
            poom_arduboy_display();
            return;
    }
}

/* ---- buttons ------------------------------------------------------------- */

static void hv_button_cb_(const poom_sbus_msg_t *msg, void *user_ctx)
{
    hv_button_msg_t ev;

    (void)user_ctx;

    if ((msg == NULL) || (msg->len < sizeof(ev)))
    {
        return;
    }
    (void)memcpy(&ev, msg->data, sizeof(ev));
    if (ev.event != BUTTON_SINGLE_CLICK)
    {
        return;
    }

    if (ev.button == BTN_B)
    {
        switch (s_state)
        {
            case HV_LIST:
                if (s_task == NULL)
                {
                    (void)hv_exit_();
                }
                else
                {
                    s_exit_requested = true;
                }
                break;
            case HV_DETAIL:
                s_state = HV_LIST;
                break;
            case HV_PICK_PROFILE:
            case HV_EDIT_URL:
                s_state = HV_DETAIL;
                break;
            case HV_CONNECTING:
                (void)poom_wifi_ctrl_sta_disconnect();
                s_send_after_ip = false;
                s_state = HV_DETAIL;
                break;
            case HV_RESULT:
                (void)poom_wifi_ctrl_sta_disconnect();
                s_state = HV_LIST;
                break;
            default:
                break;
        }
        hv_wake_();
        return;
    }

    switch (s_state)
    {
        case HV_LIST:
            if (s_entry_count == 0U)
            {
                if (ev.button == BTN_A)
                {
                    s_rescan_pending = true;
                }
                break;
            }
            if (ev.button == BTN_UP)
            {
                if (s_entry_sel > 0U) { s_entry_sel--; }
            }
            else if (ev.button == BTN_DOWN)
            {
                if (s_entry_sel < (uint16_t)(s_entry_count - 1U)) { s_entry_sel++; }
            }
            else if (ev.button == BTN_A)
            {
                s_state = HV_DETAIL;
            }
            break;

        case HV_DETAIL:
            if (ev.button == BTN_A)
            {
                hv_begin_send_();
            }
            else if (ev.button == BTN_RIGHT)
            {
                hv_enter_edit_url_();
            }
            break;

        case HV_PICK_PROFILE:
            if (ev.button == BTN_UP)
            {
                if (s_profile_sel > 0U) { s_profile_sel--; }
            }
            else if (ev.button == BTN_DOWN)
            {
                if (s_profile_sel < (uint16_t)(s_profile_count - 1U)) { s_profile_sel++; }
            }
            else if (ev.button == BTN_A)
            {
                hv_request_connect_(s_profiles[s_profile_sel].ssid,
                                    s_profiles[s_profile_sel].pass);
            }
            break;

        case HV_EDIT_URL:
        {
            poom_ui_keyboard_action_t action =
                poom_ui_keyboard_handle_button(&s_keyboard, ev.button);
            if (action == POOM_UI_KEYBOARD_ACTION_ACCEPT)
            {
                (void)snprintf(s_pi_url, sizeof(s_pi_url), "%.*s",
                               (int)(HV_URL_CAP - 1U), s_edit_url);
                (void)poom_secrets_set_str(HV_NVS_KEY, s_pi_url);
                s_state = HV_DETAIL;
            }
            break;
        }

        case HV_RESULT:
            if (ev.button == BTN_A)
            {
                (void)poom_wifi_ctrl_sta_disconnect();
                s_state = HV_LIST;
            }
            break;

        default:
            break;
    }

    hv_wake_();
}

/* ---- wifi event cb ------------------------------------------------------- */

static void hv_wifi_evt_cb_(const poom_wifi_ctrl_evt_info_t *info, void *user_ctx)
{
    (void)user_ctx;
    (void)info;
}

/* ---- teardown ------------------------------------------------------------ */

static esp_err_t hv_exit_(void)
{
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    TaskHandle_t to_delete = NULL;
    const uint8_t token = 1;

    s_active = false;
    s_exit_requested = false;
    s_rescan_pending = false;
    s_connect_request_pending = false;
    s_send_pending = false;
    s_send_after_ip = false;

    portENTER_CRITICAL(&s_task_lock);
    if ((s_task != NULL) && (s_task != current))
    {
        to_delete = s_task;
    }
    s_task = NULL;
    portEXIT_CRITICAL(&s_task_lock);

    if (to_delete != NULL)
    {
        vTaskDelete(to_delete);
    }

    if (s_buttons_subscribed)
    {
        (void)poom_sbus_unsubscribe_cb("input/button", hv_button_cb_, s_sbus_user);
        s_buttons_subscribed = false;
    }

    (void)poom_wifi_ctrl_sta_disconnect();
    (void)poom_wifi_ctrl_unregister_cb();

    (void)poom_sbus_publish(POOM_MENU_RESUME_TOPIC, &token, sizeof(token), 0);
    return ESP_OK;
}

/* ---- task ---------------------------------------------------------------- */

static void hv_task_(void *arg)
{
    (void)arg;

    while (s_active)
    {
        if (s_exit_requested)
        {
            (void)hv_exit_();
            break;
        }

        if (s_rescan_pending)
        {
            s_rescan_pending = false;
            hv_rescan_();
        }

        if (s_connect_request_pending)
        {
            esp_err_t status;

            s_connect_request_pending = false;
            (void)poom_wifi_ctrl_set_promiscuous(false);
            printf("[HV] join %u/%u ssid='%s' pass=%s\n",
                   (unsigned)s_connect_attempt, (unsigned)HV_CONNECT_ATTEMPTS,
                   s_connect_ssid, (s_connect_pass[0] != '\0') ? "yes" : "none");
            status = poom_wifi_ctrl_sta_connect(
                s_connect_ssid,
                (s_connect_pass[0] != '\0') ? s_connect_pass : NULL);
            printf("[HV] sta_connect rc=%s\n", esp_err_to_name(status));
            if (status != ESP_OK)
            {
                s_send_after_ip = false;
                hv_begin_info_("Connect failed");
            }
            else
            {
                s_connect_start_ms = hv_now_ms_();
                s_state = HV_CONNECTING;
            }
        }

        if (s_state == HV_CONNECTING)
        {
            hv_update_connect_();
        }

        if (s_send_pending)
        {
            s_send_pending = false;
            s_state = HV_SENDING;
            (void)hv_draw_();
            hv_send_selected_();
        }

        (void)hv_draw_();
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HV_REFRESH_MS));

        if ((s_state == HV_INFO) && hv_info_elapsed_())
        {
            s_state = HV_LIST;
        }
    }

    portENTER_CRITICAL(&s_task_lock);
    if (s_task == xTaskGetCurrentTaskHandle())
    {
        s_task = NULL;
    }
    portEXIT_CRITICAL(&s_task_lock);
    vTaskDelete(NULL);
}

void app_hs_vault(void)
{
    size_t url_len = sizeof(s_pi_url);
    bool create;

    if (s_active)
    {
        return;
    }

    s_active = true;
    s_exit_requested = false;
    s_rescan_pending = true; /* scan SD in task context on first loop */
    s_connect_request_pending = false;
    s_send_pending = false;
    s_send_after_ip = false;
    s_state = HV_LIST;
    s_entry_count = 0U;
    s_entry_sel = 0U;
    s_entry_window = 0U;
    s_status[0] = '\0';

    /* Load the persisted Pi endpoint, else the placeholder default. */
    (void)poom_secrets_init();
    if ((poom_secrets_get_str(HV_NVS_KEY, s_pi_url, &url_len) != ESP_OK) || (s_pi_url[0] == '\0'))
    {
        (void)snprintf(s_pi_url, sizeof(s_pi_url), "%s", HV_DEF_URL);
    }

    (void)poom_wifi_ctrl_init_sta();
    (void)poom_wifi_ctrl_set_promiscuous(false); /* clear monitor mode a capture app may have left on */
    poom_wifi_profile_store_load(s_profiles, POOM_WIFI_PROFILE_MAX, &s_profile_count);
    (void)poom_wifi_ctrl_register_cb(hv_wifi_evt_cb_, NULL);
    printf("[HV] start: %u profile(s)\n", (unsigned)s_profile_count);

    if (!s_buttons_subscribed)
    {
        if (poom_sbus_subscribe_cb("input/button", hv_button_cb_, s_sbus_user))
        {
            s_buttons_subscribed = true;
        }
        else
        {
            const uint8_t token = 1;
            s_active = false;
            (void)poom_sbus_publish(POOM_MENU_RESUME_TOPIC, &token, sizeof(token), 0);
            return;
        }
    }

    portENTER_CRITICAL(&s_task_lock);
    create = (s_task == NULL);
    portEXIT_CRITICAL(&s_task_lock);

    if (create)
    {
        TaskHandle_t created = NULL;
        if (xTaskCreate(hv_task_, "menu_hs_vault", HV_STACK, NULL, HV_PRIO, &created) == pdPASS)
        {
            portENTER_CRITICAL(&s_task_lock);
            if (s_task == NULL)
            {
                s_task = created;
                created = NULL;
            }
            portEXIT_CRITICAL(&s_task_lock);
            if (created != NULL)
            {
                vTaskDelete(created);
            }
        }
        else
        {
            const uint8_t token = 1;
            s_active = false;
            (void)poom_sbus_publish(POOM_MENU_RESUME_TOPIC, &token, sizeof(token), 0);
        }
    }
}
