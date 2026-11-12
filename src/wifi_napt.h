// WiFi SoftAP + NAPT: the hotspot the ambytes join, with NAT to the PPP uplink.
#pragma once
#include "esp_err.h"

// Brings up the SoftAP (always-on, independent of cellular) and registers the
// IP-event handler that, once PPP has an IP, hands carrier DNS to DHCP clients
// and enables NAPT so their traffic is routed over the modem.
// Call after esp_netif_init() and esp_event_loop_create_default().
esp_err_t wifi_napt_start(void);

// Number of currently-associated clients (for logs / status LED).
int wifi_napt_client_count(void);
