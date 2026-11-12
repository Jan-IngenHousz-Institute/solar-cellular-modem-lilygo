// Gateway settings saved in NVS, with compiled defaults as fallback.

#include <string.h>
#include <stdio.h>

#include "nvs.h"
#include "esp_log.h"

#include "app_config.h"
#include "settings.h"

static const char *TAG = "settings";

#define NVS_NS        "gateway"
#define KEY_SSID      "ssid"
#define KEY_PASSWORD  "password"
#define KEY_APN       "apn"
#define KEY_APN_USER  "apn_user"
#define KEY_APN_PASS  "apn_pass"

static gateway_settings_t s_cur;
static bool s_configured = false;
static bool s_pending    = false;

const gateway_settings_t *settings_get(void) { return &s_cur; }
bool settings_is_configured(void)            { return s_configured; }
bool settings_reboot_pending(void)           { return s_pending; }

// Printable ASCII only: the console drops non-ASCII bytes, and WPA2
// passphrases must be ASCII anyway.
static bool printable_ascii(const char *s, bool allow_space)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7e) return false;
        if (c == ' ' && !allow_space) return false;
    }
    return true;
}

const char *settings_check_wifi(const char *ssid, const char *password)
{
    size_t n = strlen(ssid);
    if (n < 1 || n > SETTINGS_SSID_MAX) return "WiFi name must be 1-32 characters";
    if (!printable_ascii(ssid, true))   return "WiFi name may only use plain letters, digits, spaces and symbols (no accents or emoji)";

    size_t p = strlen(password);
    if (p == 0) return NULL;                        // open hotspot
    if (p < 8 || p > SETTINGS_PASS_MAX) return "Password must be 8-63 characters";
    if (!printable_ascii(password, true)) return "Password may only use plain letters, digits, spaces and symbols (no accents or emoji)";
    return NULL;
}

const char *settings_check_apn(const char *apn, const char *user, const char *pass)
{
    size_t n = strlen(apn);
    if (n < 1 || n > SETTINGS_APN_MAX) return "APN must be 1-63 characters";
    if (!printable_ascii(apn, false))  return "APN may only use plain letters, digits and symbols, without spaces";
    if (strlen(user) > SETTINGS_CRED_MAX || strlen(pass) > SETTINGS_CRED_MAX)
        return "APN username and password must be at most 63 characters";
    if (!printable_ascii(user, true) || !printable_ascii(pass, true))
        return "APN username and password may only use plain letters, digits, spaces and symbols";
    return NULL;
}

static void load_str(nvs_handle_t h, bool have_nvs, const char *key,
                     char *dst, size_t cap, const char *fallback)
{
    size_t len = cap;
    if (!have_nvs || nvs_get_str(h, key, dst, &len) != ESP_OK) {
        snprintf(dst, cap, "%s", fallback);
    }
}

void settings_load(void)
{
    nvs_handle_t h = 0;
    bool have_nvs = (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK);   // absent until first save

    load_str(h, have_nvs, KEY_SSID,     s_cur.ssid,     sizeof(s_cur.ssid),     WIFI_AP_SSID);
    load_str(h, have_nvs, KEY_PASSWORD, s_cur.password, sizeof(s_cur.password), WIFI_AP_PASSWORD);
    load_str(h, have_nvs, KEY_APN,      s_cur.apn,      sizeof(s_cur.apn),      CELL_APN);
    load_str(h, have_nvs, KEY_APN_USER, s_cur.apn_user, sizeof(s_cur.apn_user), CELL_APN_USER);
    load_str(h, have_nvs, KEY_APN_PASS, s_cur.apn_pass, sizeof(s_cur.apn_pass), CELL_APN_PASS);
    if (have_nvs) nvs_close(h);

    s_configured = false;
    if (s_cur.ssid[0] == '\0') {
        ESP_LOGW(TAG, "no hotspot name set");
        return;
    }
    // Guard against bad compiled defaults too: an invalid password would make
    // esp_wifi_set_config() fail and the board boot-loop.
    const char *why = settings_check_wifi(s_cur.ssid, s_cur.password);
    if (!why) why = settings_check_apn(s_cur.apn, s_cur.apn_user, s_cur.apn_pass);
    if (why) {
        ESP_LOGE(TAG, "settings invalid: %s", why);
        return;
    }
    s_configured = true;
}

static esp_err_t save_strs(const char *const keys[], const char *const vals[], int n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    for (int i = 0; i < n && err == ESP_OK; i++) {
        err = nvs_set_str(h, keys[i], vals[i]);
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_pending = true;
    return err;
}

esp_err_t settings_save_wifi(const char *ssid, const char *password)
{
    if (settings_check_wifi(ssid, password)) return ESP_ERR_INVALID_ARG;
    const char *const keys[] = { KEY_SSID, KEY_PASSWORD };
    const char *const vals[] = { ssid, password };
    return save_strs(keys, vals, 2);
}

esp_err_t settings_save_apn(const char *apn, const char *user, const char *pass)
{
    if (settings_check_apn(apn, user, pass)) return ESP_ERR_INVALID_ARG;
    const char *const keys[] = { KEY_APN, KEY_APN_USER, KEY_APN_PASS };
    const char *const vals[] = { apn, user, pass };
    return save_strs(keys, vals, 3);
}

esp_err_t settings_factory_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_pending = true;
    return err;
}
