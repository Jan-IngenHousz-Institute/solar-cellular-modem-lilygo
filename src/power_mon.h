// Battery/solar ADC telemetry, status-LED patterns, and optional low-battery
// deep-sleep (good for an unattended solar device).
#pragma once
#include "esp_err.h"

// Starts the ADC sampling task and the LED task. Call after the WiFi AP and the
// modem supervisor have started (the LED reflects modem_ppp_state()).
esp_err_t power_mon_start(void);

int power_mon_batt_mv(void);    // last battery reading in millivolts (rail, divider undone)
int power_mon_solar_mv(void);   // last solar-panel reading in millivolts
