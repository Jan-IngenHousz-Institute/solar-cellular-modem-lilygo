// Cellular modem (SIM7000G) PPP uplink + self-healing reconnect supervisor.
#pragma once
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    UPLINK_DOWN = 0,     // modem off / booting / unresponsive
    UPLINK_CONNECTING,   // registered or dialing, no IP yet
    UPLINK_UP,           // PPP has an IP — internet is being routed
} uplink_state_t;

// Powers on the modem and launches the supervisor task. Call after esp_netif_init()
// and esp_event_loop_create_default(). Returns immediately; connection is async.
esp_err_t modem_ppp_start(void);

// Current uplink state (for the status LED / logs).
uplink_state_t modem_ppp_state(void);

// Last signal quality read before dialing (CSQ 0-31; 99 = unknown). The modem
// can't be queried while it carries PPP data, so this is the value at connect.
int modem_ppp_signal_csq(void);

// Writes the cellular IP into buf ("" when offline). 16 bytes is enough.
void modem_ppp_ip_str(char *buf, size_t len);
