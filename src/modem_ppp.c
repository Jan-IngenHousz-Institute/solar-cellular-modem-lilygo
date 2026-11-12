// Cellular modem (SIM7000G) PPP uplink + self-healing reconnect supervisor.
//
// SIM7000 has NO CMUX, so we use plain command mode for setup, then switch the
// UART to DATA (PPP) mode. We never issue AT commands while in DATA mode.
//
// The supervisor task owns the modem lifecycle: power-on, register, dial, then
// sit ONLINE until PPP drops; on failure it retries with backoff, power-cycles
// the modem after a few failures, and reboots after many. The WiFi AP is run by
// wifi_napt.c and stays up the whole time, independent of the cellular link.

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_modem_api.h"
#include "esp_modem_config.h"

#include "app_config.h"
#include "modem_ppp.h"

static const char *TAG = "modem";

#define GOTIP_BIT   BIT0
#define LOSTIP_BIT  BIT1

static esp_modem_dce_t   *s_dce;
static esp_netif_t       *s_ppp_netif;
static EventGroupHandle_t s_eg;
static volatile uplink_state_t s_state = UPLINK_DOWN;

uplink_state_t modem_ppp_state(void) { return s_state; }

// ---------------------------------------------------------------------------
//  PWRKEY power sequencing
//
//  On the T-SIM7000G, GPIO4 drives the SIM7000 PWRKEY through an inverting
//  transistor, and a >1 s pulse TOGGLES power. Because a soft reset can leave
//  the modem already on, we never blindly pulse: bring-up first checks whether
//  the modem answers AT, and only pulses if it's silent (see modem_ensure_on).
//
//  If a board revision never enumerates, flip PWRKEY_ASSERT.
// ---------------------------------------------------------------------------
#define PWRKEY_ASSERT   1    // GPIO level that asserts the chip PWRKEY

static void pwrkey_pulse(void)
{
    gpio_set_level(PIN_MODEM_PWRKEY, !PWRKEY_ASSERT);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_MODEM_PWRKEY, PWRKEY_ASSERT);
    vTaskDelay(pdMS_TO_TICKS(1100));     // Ton >= 1 s for SIM7000
    gpio_set_level(PIN_MODEM_PWRKEY, !PWRKEY_ASSERT);
    vTaskDelay(pdMS_TO_TICKS(3000));     // let the modem boot its firmware
}

// ---------------------------------------------------------------------------
//  DCE / netif lifecycle
// ---------------------------------------------------------------------------
static bool create_dce(void)
{
    esp_netif_config_t ppp_cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&ppp_cfg);
    if (!s_ppp_netif) {
        ESP_LOGE(TAG, "esp_netif_new(PPP) failed");
        return false;
    }

    esp_modem_dte_config_t dte = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte.uart_config.port_num         = UART_NUM_1;
    dte.uart_config.tx_io_num        = PIN_MODEM_TX;
    dte.uart_config.rx_io_num        = PIN_MODEM_RX;
    dte.uart_config.rts_io_num       = -1;
    dte.uart_config.cts_io_num       = -1;
    dte.uart_config.flow_control     = ESP_MODEM_FLOW_CONTROL_NONE;
    dte.uart_config.baud_rate        = MODEM_UART_BAUD;
    dte.uart_config.rx_buffer_size   = 4096;
    dte.uart_config.tx_buffer_size   = 1024;
    dte.uart_config.event_queue_size = 30;

    esp_modem_dce_config_t dce_cfg = ESP_MODEM_DCE_DEFAULT_CONFIG(CELL_APN);
    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte, &dce_cfg, s_ppp_netif);
    if (!s_dce) {
        ESP_LOGE(TAG, "esp_modem_new_dev failed");
        esp_netif_destroy(s_ppp_netif);
        s_ppp_netif = NULL;
        return false;
    }
    return true;
}

static void destroy_dce(void)
{
    if (s_dce)       { esp_modem_destroy(s_dce); s_dce = NULL; }
    if (s_ppp_netif) { esp_netif_destroy(s_ppp_netif); s_ppp_netif = NULL; }
}

// Returns true if the modem answers AT within `tries`.
static bool modem_alive(int tries)
{
    for (int i = 0; i < tries; i++) {
        if (esp_modem_sync(s_dce) == ESP_OK) return true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

// Make sure the modem is powered on and responsive, resolving the PWRKEY toggle
// ambiguity: if it's silent we pulse (off->on); if it was already on, the first
// pulse turns it off and the second turns it back on.
static bool modem_ensure_on(void)
{
    if (modem_alive(3)) { ESP_LOGI(TAG, "modem already responsive"); return true; }
    for (int p = 0; p < 2; p++) {
        ESP_LOGI(TAG, "PWRKEY pulse %d/2", p + 1);
        pwrkey_pulse();
        if (modem_alive(6)) return true;
    }
    ESP_LOGE(TAG, "modem unresponsive after power pulses");
    return false;
}

// Wait for network registration (CSQ), then switch to DATA (PPP) mode.
static bool register_and_dial(void)
{
    int rssi = 99, ber = 99;
    int64_t t0 = esp_timer_get_time();
    while ((esp_timer_get_time() - t0) < (int64_t)MODEM_REG_TIMEOUT_S * 1000000) {
        esp_task_wdt_reset();
        if (esp_modem_get_signal_quality(s_dce, &rssi, &ber) == ESP_OK &&
            rssi != 99 && rssi > 0) {
            ESP_LOGI(TAG, "network registered (CSQ rssi=%d)", rssi);
            break;
        }
        ESP_LOGI(TAG, "waiting for network (CSQ=%d) ...", rssi);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    if (rssi == 99 || rssi <= 0) {
        ESP_LOGW(TAG, "no registration yet; attempting PPP anyway");
    }

    ESP_LOGI(TAG, "switching to DATA (PPP) mode, APN='%s'", CELL_APN);
    if (esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA) != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_set_mode(DATA) failed");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  PPP IP events
// ---------------------------------------------------------------------------
static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_PPP_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "PPP GOT IP " IPSTR " gw " IPSTR,
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        // Route the gateway's own traffic (and the NAT target) out over cellular.
        esp_netif_set_default_netif(e->esp_netif);
        xEventGroupSetBits(s_eg, GOTIP_BIT);
    } else if (id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGW(TAG, "PPP LOST IP");
        xEventGroupSetBits(s_eg, LOSTIP_BIT);
    }
}

// ---------------------------------------------------------------------------
//  Supervisor
// ---------------------------------------------------------------------------
static void supervisor_task(void *arg)
{
    esp_task_wdt_add(NULL);
    int fails = 0;

    while (1) {
        esp_task_wdt_reset();
        s_state = UPLINK_DOWN;
        xEventGroupClearBits(s_eg, GOTIP_BIT | LOSTIP_BIT);

        if (!create_dce())      { fails++; goto escalate; }
        if (!modem_ensure_on()) { destroy_dce(); fails++; goto escalate; }

        s_state = UPLINK_CONNECTING;
        if (!register_and_dial()) { destroy_dce(); fails++; goto escalate; }

        // Wait for an IP (PPP negotiation), feeding the watchdog.
        EventBits_t bits = 0;
        int64_t t0 = esp_timer_get_time();
        while ((esp_timer_get_time() - t0) < (int64_t)MODEM_GOTIP_TIMEOUT_S * 1000000) {
            esp_task_wdt_reset();
            bits = xEventGroupWaitBits(s_eg, GOTIP_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));
            if (bits & GOTIP_BIT) break;
        }
        if (!(bits & GOTIP_BIT)) {
            ESP_LOGW(TAG, "no PPP IP within %d s", MODEM_GOTIP_TIMEOUT_S);
            destroy_dce();
            fails++;
            goto escalate;
        }

        // ---- ONLINE ----
        fails = 0;
        s_state = UPLINK_UP;
        ESP_LOGI(TAG, "UPLINK UP — routing clients over cellular");
        while (1) {
            esp_task_wdt_reset();
            EventBits_t lost = xEventGroupWaitBits(s_eg, LOSTIP_BIT, pdTRUE, pdFALSE,
                                                   pdMS_TO_TICKS(5000));
            if (lost & LOSTIP_BIT) { ESP_LOGW(TAG, "uplink dropped"); break; }
        }
        s_state = UPLINK_CONNECTING;
        destroy_dce();                     // tear down; loop rebuilds (modem stays powered)
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;

    escalate:
        ESP_LOGW(TAG, "bring-up failed (fail count = %d)", fails);
        if (fails >= MODEM_FAILS_BEFORE_REBOOT) {
            ESP_LOGE(TAG, "too many failures — rebooting");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        if ((fails % MODEM_FAILS_BEFORE_PWRCYCLE) == 0) {
            ESP_LOGW(TAG, "power-cycling the modem");
            pwrkey_pulse();                // toggle off
            vTaskDelay(pdMS_TO_TICKS(2000));
            pwrkey_pulse();                // toggle on
        }
        int backoff_s = fails * 2;
        if (backoff_s > 30) backoff_s = 30;
        vTaskDelay(pdMS_TO_TICKS(backoff_s * 1000 + 1000));
    }
}

esp_err_t modem_ppp_start(void)
{
    // PWRKEY idle level (de-asserted) before any pulse.
    gpio_reset_pin(PIN_MODEM_PWRKEY);
    gpio_set_direction(PIN_MODEM_PWRKEY, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_MODEM_PWRKEY, !PWRKEY_ASSERT);

    s_eg = xEventGroupCreate();
    if (!s_eg) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP,  on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, on_ip_event, NULL));

    if (xTaskCreate(supervisor_task, "modem_sup", 5120, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
