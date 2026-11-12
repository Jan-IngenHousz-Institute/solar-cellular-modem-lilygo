// Serial command line (USB serial, 115200 baud) for status checks and setup.
#pragma once
#include "esp_err.h"

// Starts the console. Call early in app_main (after settings_load) so the board
// can be set up even while unconfigured.
esp_err_t cli_start(void);
