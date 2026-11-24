// WiFi SoftAP + NAPT.
//
// The AP comes up immediately at boot (so ambytes can associate even before the
// cellular link is ready). When PPP gets an IP we (1) point the AP's DHCP server
// at the carrier's DNS for new leases, and (2) enable NAPT on the AP netif so
// client traffic is NAT-routed out the (default) PPP interface.
//
// Mirrors Espressif's esp_modem/examples/ap_to_pppos, using the current
// esp_netif_napt_enable() wrapper instead of the raw lwIP ip_napt_enable().

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "dhcpserver/dhcpserver.h"

#include "app_config.h"
#include "settings.h"
#include "wifi_napt.h"

static const char *TAG = "wifiap";

static esp_netif_t   *s_ap_netif;
static volatile int   s_clients = 0;
static bool           s_napt_on = false;

int wifi_napt_client_count(void) { return s_clients; }

// Set the DNS server the DHCP server hands out (DHCP option 6).
//
// This only changes what LATER leases carry, and is safe while the server runs.
// Never stop/start the DHCP server to do this: stopping it frees its lease
// table, so the next device to join is offered an address that is still in use
// (seen on hardware: a phone needed 70 s and three attempts to get an address).
static void set_client_dns(esp_ip4_addr_t dns)
{
    esp_netif_dns_info_t info = { 0 };
    info.ip.type = ESP_IPADDR_TYPE_V4;
    info.ip.u_addr.ip4 = dns;
    esp_err_t err = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &info);
    if (err != ESP_OK) ESP_LOGW(TAG, "could not set client DNS: %s", esp_err_to_name(err));
    else               ESP_LOGI(TAG, "client DNS = " IPSTR, IP2STR(&dns));
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        s_clients++;
        ESP_LOGI(TAG, "client joined  (now %d)", s_clients);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_clients > 0) s_clients--;
        ESP_LOGI(TAG, "client left    (now %d)", s_clients);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != IP_EVENT_PPP_GOT_IP) return;
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;

    // Prefer the carrier-supplied DNS; fall back to the configured public one.
    esp_netif_dns_info_t dns = { 0 };
    esp_ip4_addr_t use;
    if (esp_netif_get_dns_info(e->esp_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.u_addr.ip4.addr != 0) {
        use = dns.ip.u_addr.ip4;
    } else {
        esp_netif_str_to_ip4(FALLBACK_DNS, &use);
    }
    set_client_dns(use);

    if (!s_napt_on) {
        esp_err_t err = esp_netif_napt_enable(s_ap_netif);
        if (err == ESP_OK) {
            s_napt_on = true;
            ESP_LOGI(TAG, "NAPT enabled on SoftAP — clients now have internet");
        } else {
            ESP_LOGE(TAG, "esp_netif_napt_enable failed: %s "
                          "(is CONFIG_LWIP_IPV4_NAPT=y?)", esp_err_to_name(err));
        }
    }
}

esp_err_t wifi_napt_start(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP,
                                               on_ip_event, NULL));

    const gateway_settings_t *st = settings_get();
    bool open = (st->password[0] == '\0');
    wifi_config_t wc = { 0 };
    // ssid is a 32-byte field that need not be NUL-terminated (length in ssid_len).
    size_t ssid_len = strlen(st->ssid);
    memcpy(wc.ap.ssid, st->ssid, ssid_len);
    wc.ap.ssid_len       = ssid_len;
    wc.ap.channel        = WIFI_AP_CHANNEL;
    wc.ap.max_connection = WIFI_AP_MAX_CONN;
    wc.ap.beacon_interval = 100;
    if (open) {
        wc.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        snprintf((char *)wc.ap.password, sizeof(wc.ap.password), "%s", st->password);
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    // DHCP: include a DNS server in every lease. Options can't be set once the
    // DHCP server runs, so do it here, once, before the AP (and with it the DHCP
    // server) starts. Do NOT call esp_netif_dhcps_stop() first: on a server that
    // has not started yet it means "keep it off", and no client gets an address.
    dhcps_offer_t offer = OFFER_DNS;
    esp_err_t opt = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                                           ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    if (opt != ESP_OK) ESP_LOGE(TAG, "DHCP DNS option not set: %s", esp_err_to_name(opt));

    // Give clients a resolver even before cellular is up (replaced by the carrier's
    // DNS on PPP GOT_IP). Reachable through NAT once the uplink is online.
    esp_ip4_addr_t fb;
    esp_netif_str_to_ip4(FALLBACK_DNS, &fb);
    set_client_dns(fb);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP '%s' up (%s), max %d clients",
             st->ssid, open ? "OPEN" : "WPA2-PSK", WIFI_AP_MAX_CONN);
    return ESP_OK;
}
