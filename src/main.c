// T-SIM7000G  —  transparent cellular -> WiFi NAT router.
//
// Brings up a WiFi hotspot and shares the SIM7000G's cellular internet to the
// devices that join it (the "ambyte" ESP32-S3 nodes). Those nodes make their own
// end-to-end MQTT-over-TLS connections to openJII; this gateway only NAT-routes
// packets and holds no openJII secrets.
//
// Init order matters: the SoftAP comes up FIRST so ambytes can associate even
// before the (slower) cellular link is ready.

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "app_config.h"
#include "wifi_napt.h"
#include "modem_ppp.h"
#include "power_mon.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "===== T-SIM7000G cellular->WiFi NAT router =====");
    ESP_LOGI(TAG, "AP SSID '%s'  |  APN '%s'", WIFI_AP_SSID, CELL_APN);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(wifi_napt_start());   // 1) hotspot up first — clients can join immediately
    ESP_ERROR_CHECK(power_mon_start());   // 2) battery/solar telemetry + status LED
    ESP_ERROR_CHECK(modem_ppp_start());   // 3) cellular PPP + self-healing supervisor

    ESP_LOGI(TAG, "init complete; supervisor is bringing up the uplink");
}
