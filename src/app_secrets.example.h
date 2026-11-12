// ============================================================================
//  app_secrets.example.h  —  TEMPLATE for optional factory defaults.
//
//  Copy to app_secrets.h (gitignored) to bake a default hotspot into YOUR build,
//  e.g. for bench testing. Not needed otherwise: the hotspot is normally set on
//  the board (setup page or serial 'wifi set'), and settings saved there win.
//  Without app_secrets.h the board boots unconfigured (hotspot and modem off).
// ============================================================================
#pragma once

// WiFi hotspot the ambytes join. Give each gateway its own SSID.
#define WIFI_AP_SSID        "AmbitGateway"
#define WIFI_AP_PASSWORD    "change-me"           // >=8 chars for WPA2, or "" for an open AP

// Cellular APN login — only if the SIM needs it (usually empty).
#define CELL_APN_USER       ""
#define CELL_APN_PASS       ""
