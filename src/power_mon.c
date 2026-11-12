// Battery/solar ADC telemetry, status-LED patterns, low-battery deep-sleep.
//
// LED (GPIO12, active-low):
//   fast blink   -> modem down / booting / unresponsive (UPLINK_DOWN)
//   slow blink   -> registered or dialing, no IP yet     (UPLINK_CONNECTING)
//   solid on     -> online, routing clients              (UPLINK_UP)
//   double-blink -> low battery (about to deep-sleep)
//   triple-blink -> needs setup (no hotspot name / APN saved yet)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "app_config.h"
#include "settings.h"
#include "modem_ppp.h"
#include "wifi_napt.h"
#include "power_mon.h"

static const char *TAG = "power";

// ADC_ATTEN_DB_11 was renamed to ADC_ATTEN_DB_12 in ESP-IDF 5.2 (DB_11 is now a
// deprecated alias). These are enum constants, not macros, so a #ifndef probe
// can't detect them — gate on the IDF version instead to build clean on either.
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#define APP_ADC_ATTEN  ADC_ATTEN_DB_12
#else
#define APP_ADC_ATTEN  ADC_ATTEN_DB_11
#endif

#define BAT_CH    ADC_CHANNEL_7   // GPIO35
#define SOLAR_CH  ADC_CHANNEL_0   // GPIO36

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t          s_cali;
static bool                       s_cali_ok = false;

static volatile int  s_batt_mv  = 0;
static volatile int  s_solar_mv = 0;
static volatile bool s_low_batt = false;
static int           s_low_count = 0;   // consecutive in-window low readings (debounce)

int power_mon_batt_mv(void)  { return s_batt_mv; }
int power_mon_solar_mv(void) { return s_solar_mv; }

// Read a channel, convert to millivolts at the pin, then undo the on-board divider.
static int read_rail_mv(adc_channel_t ch)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) return -1;

    int mv = 0;
    if (!(s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK)) {
        mv = (int)(raw * 3300.0f / 4095.0f);   // uncalibrated fallback (12-bit, ~3.3 V FS)
    }
    return (int)(mv * ADC_DIVIDER_RATIO);
}

static inline void led(bool on)
{
    gpio_set_level(PIN_LED, on ? LED_ON_LEVEL : !LED_ON_LEVEL);
}

static void led_task(void *arg)
{
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    led(false);

    while (1) {
        if (s_low_batt) {                                   // double-blink
            led(true);  vTaskDelay(pdMS_TO_TICKS(80));
            led(false); vTaskDelay(pdMS_TO_TICKS(80));
            led(true);  vTaskDelay(pdMS_TO_TICKS(80));
            led(false); vTaskDelay(pdMS_TO_TICKS(760));
            continue;
        }
        if (!settings_is_configured()) {                    // triple-blink
            for (int i = 0; i < 3; i++) {
                led(true);  vTaskDelay(pdMS_TO_TICKS(80));
                led(false); vTaskDelay(pdMS_TO_TICKS(160));
            }
            vTaskDelay(pdMS_TO_TICKS(1200));
            continue;
        }
        switch (modem_ppp_state()) {
        case UPLINK_UP:                                     // solid
            led(true);  vTaskDelay(pdMS_TO_TICKS(500));
            break;
        case UPLINK_CONNECTING:                             // slow blink
            led(true);  vTaskDelay(pdMS_TO_TICKS(120));
            led(false); vTaskDelay(pdMS_TO_TICKS(880));
            break;
        case UPLINK_DOWN:                                   // fast blink
        default:
            led(true);  vTaskDelay(pdMS_TO_TICKS(120));
            led(false); vTaskDelay(pdMS_TO_TICKS(120));
            break;
        }
    }
}

static void sample_task(void *arg)
{
    while (1) {
        s_batt_mv  = read_rail_mv(BAT_CH);
        s_solar_mv = read_rail_mv(SOLAR_CH);
        ESP_LOGI(TAG, "batt=%d mV  solar=%d mV  clients=%d  uplink=%d",
                 s_batt_mv, s_solar_mv, wifi_napt_client_count(), modem_ppp_state());

        // Low-battery deep-sleep, with two guards so it never fires spuriously:
        //   * validity floor — a reading below BATT_VALID_MIN_MV means there is no
        //     battery on the sense pin (USB-powered / floating ADC, e.g. on the
        //     bench), NOT a flat cell, so we must NOT sleep.
        //   * debounce — require BATT_LOW_SAMPLES consecutive in-window readings.
        bool batt_low = (BATT_LOW_MV > 0) &&
                        (s_batt_mv >= BATT_VALID_MIN_MV) &&
                        (s_batt_mv <  BATT_LOW_MV);
        if (batt_low) {
            s_low_batt = true;
            if (++s_low_count >= BATT_LOW_SAMPLES) {
                ESP_LOGW(TAG, "battery low (%d mV < %d, %dx) — deep-sleeping %d s for solar recharge",
                         s_batt_mv, BATT_LOW_MV, s_low_count, BATT_SLEEP_SECS);
                vTaskDelay(pdMS_TO_TICKS(2000));            // let the LED + log flush
                esp_sleep_enable_timer_wakeup((uint64_t)BATT_SLEEP_SECS * 1000000ULL);
                esp_deep_sleep_start();                     // wakes via full reset -> re-init
            }
        } else {
            s_low_batt = false;
            s_low_count = 0;
        }
        vTaskDelay(pdMS_TO_TICKS((TickType_t)POWER_SAMPLE_SECS * 1000));
    }
}

esp_err_t power_mon_start(void)
{
    adc_oneshot_unit_init_cfg_t init = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init, &s_adc));

    adc_oneshot_chan_cfg_t ch = { .atten = APP_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BAT_CH, &ch));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, SOLAR_CH, &ch));

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali = {
        .unit_id  = ADC_UNIT_1,
        .atten    = APP_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali, &s_cali) == ESP_OK) {
        s_cali_ok = true;
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable; using uncalibrated estimate");
    }
#endif

    if (xTaskCreate(led_task,    "led",   2048, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sample_task, "power", 3072, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
