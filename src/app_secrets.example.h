// ============================================================================
//  app_secrets.example.h  —  TEMPLATE. Copy to app_secrets.h and fill in.
//  app_secrets.h is gitignored; this template is the only copy committed.
// ============================================================================
#pragma once

// WiFi hotspot the ambytes join. Give each gateway its own SSID.
#define WIFI_AP_SSID        "AmbitGateway"
#define WIFI_AP_PASSWORD    "change-me"           // >=8 chars for WPA2, or "" for an open AP

// Cellular APN login — only if the SIM needs it (usually empty).
#define CELL_APN_USER       ""
#define CELL_APN_PASS       ""
