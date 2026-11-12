// ============================================================================
//  app_config.h  —  device settings (non-secret).
//
//  T-SIM7000G transparent cellular -> WiFi NAT router.
//
//  Hotspot name/password and APN are normally set on the board itself (setup
//  page or serial 'wifi set' / 'apn set') and saved in flash; those saved values
//  win. The values below are only FACTORY DEFAULTS for a freshly erased board.
//  Credential defaults come from the optional, gitignored app_secrets.h; without
//  it (e.g. the public release build) the board boots unconfigured.
// ============================================================================
#pragma once

#if __has_include("app_secrets.h")
#include "app_secrets.h"
#endif
#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID        ""                    // "" = needs setup
#endif
#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD    ""
#endif
#ifndef CELL_APN_USER
#define CELL_APN_USER       ""
#endif
#ifndef CELL_APN_PASS
#define CELL_APN_PASS       ""
#endif

// ---------------------------------------------------------------------------
//  WiFi hotspot
// ---------------------------------------------------------------------------
#define WIFI_AP_CHANNEL     6                     // 1..13
#define WIFI_AP_MAX_CONN    10                    // ESP32 SoftAP hard ceiling = 10 clients

// ---------------------------------------------------------------------------
//  Cellular (SIM7000G). APN is REQUIRED and carrier-specific — wrong APN = no data.
//  "internet" is a common default; confirm with your SIM provider.
// ---------------------------------------------------------------------------
#define CELL_APN            "internet.mobiel"     // default carrier APN (overridden by 'apn set')

#define FALLBACK_DNS        "8.8.8.8"             // handed to clients if the carrier supplies none

// ---------------------------------------------------------------------------
//  Battery / solar behaviour (solar-friendly; set BATT_LOW_MV = 0 to disable
//  the low-battery deep-sleep entirely and always run while powered).
// ---------------------------------------------------------------------------
#define BATT_LOW_MV         3400                  // deep-sleep below this (but above BATT_VALID_MIN_MV)
#define BATT_VALID_MIN_MV   2500                  // readings below this = NO battery on the sense pin
                                                  //   (USB-powered / floating ADC on the bench) -> never sleep
#define BATT_LOW_SAMPLES    3                     // consecutive low readings required before sleeping (debounce)
#define BATT_SLEEP_SECS     900                   // deep-sleep duration when below cutoff (15 min)
#define POWER_SAMPLE_SECS   30                    // battery/solar sampling + log period

// ---------------------------------------------------------------------------
//  Cellular reconnect tuning (robustness for an unattended device)
// ---------------------------------------------------------------------------
#define MODEM_REG_TIMEOUT_S          90           // wait for network registration before dialing
#define MODEM_GOTIP_TIMEOUT_S        60           // wait for a PPP IP after entering DATA mode
#define MODEM_FAILS_BEFORE_PWRCYCLE  3            // failed bring-ups before power-cycling the modem
#define MODEM_FAILS_BEFORE_REBOOT    12           // total failures before esp_restart()

// ============================================================================
//  BOARD PINS — LilyGo T-SIM7000G (verified against LilyGO utilities.h).
//  Do NOT change these for this board.
// ============================================================================
#define PIN_MODEM_TX        27    // ESP32 TX -> modem RX
#define PIN_MODEM_RX        26    // ESP32 RX <- modem TX
#define PIN_MODEM_DTR       25    // held LOW by modem_ppp.c; floating breaks PPP after a modem power-on
#define PIN_MODEM_PWRKEY    4     // power-on pulse (via inverting driver)
#define PIN_LED             12    // status LED, ACTIVE-LOW. NB: GPIO12 is a boot strapping pin.
#define LED_ON_LEVEL        0     // level that turns the LED on
#define PIN_BAT_ADC         35    // ADC1_CH7  (battery)
#define PIN_SOLAR_ADC       36    // ADC1_CH0  (solar panel)
#define MODEM_UART_BAUD     115200
#define ADC_DIVIDER_RATIO   2.0f  // on-board 1:1 divider -> Vpin = Vrail / 2
