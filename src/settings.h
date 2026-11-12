// Gateway settings: hotspot name/password and cellular APN.
//
// Saved in NVS by the serial CLI (and the web setup page, which drives the CLI).
// Saved values override the compiled defaults from app_config.h / app_secrets.h.
// The hotspot and modem are configured once at boot, so a change takes effect
// after a reboot.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define SETTINGS_SSID_MAX   32
#define SETTINGS_PASS_MAX   63
#define SETTINGS_APN_MAX    63
#define SETTINGS_CRED_MAX   63

typedef struct {
    char ssid[SETTINGS_SSID_MAX + 1];
    char password[SETTINGS_PASS_MAX + 1];   // "" = open hotspot
    char apn[SETTINGS_APN_MAX + 1];
    char apn_user[SETTINGS_CRED_MAX + 1];   // usually ""
    char apn_pass[SETTINGS_CRED_MAX + 1];   // usually ""
} gateway_settings_t;

// Loads the settings in effect for this boot. Call after nvs_flash_init().
void settings_load(void);

// Settings in effect since boot (unchanged by saves until the next reboot).
const gateway_settings_t *settings_get(void);

// True when a hotspot name and an APN are set and valid. When false, the
// hotspot and modem stay off until the board is set up.
bool settings_is_configured(void);

// True after a save in this boot: the saved values apply after a reboot.
bool settings_reboot_pending(void);

// Validation. Each returns NULL if the values are acceptable, otherwise a
// human-readable reason. An empty password means an open hotspot.
const char *settings_check_wifi(const char *ssid, const char *password);
const char *settings_check_apn(const char *apn, const char *user, const char *pass);

// Validate and save to NVS. Return ESP_ERR_INVALID_ARG if validation fails.
esp_err_t settings_save_wifi(const char *ssid, const char *password);
esp_err_t settings_save_apn(const char *apn, const char *user, const char *pass);

// Erases all saved settings (back to the compiled defaults after reboot).
esp_err_t settings_factory_reset(void);
