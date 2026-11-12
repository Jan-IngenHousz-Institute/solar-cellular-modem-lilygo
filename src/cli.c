// Serial command line (USB serial, 115200 baud) for status checks and setup.
//
// Used by people in a serial terminal, and by the web setup page (web/), which
// sends the same commands over Web Serial. Replies the page parses are printed
// as single lines starting with "OK ", "ERROR " or "STATUS ", each written in
// one call so log output from other tasks can't split them.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"

#include "settings.h"
#include "modem_ppp.h"
#include "wifi_napt.h"
#include "power_mon.h"
#include "cli.h"

static const char *TAG = "cli";

// ---------------------------------------------------------------------------
//  Output helpers
// ---------------------------------------------------------------------------
typedef struct { char buf[768]; size_t len; } line_t;

static void lp(line_t *l, const char *fmt, ...)
{
    if (l->len >= sizeof(l->buf) - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(l->buf + l->len, sizeof(l->buf) - l->len, fmt, ap);
    va_end(ap);
    if (n > 0) l->len += n;
    if (l->len >= sizeof(l->buf)) l->len = sizeof(l->buf) - 1;
}

static void lp_json_str(line_t *l, const char *s)
{
    lp(l, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') lp(l, "\\%c", c);
        else if (c < 0x20)         lp(l, "\\u%04x", c);
        else                       lp(l, "%c", c);
    }
    lp(l, "\"");
}

static void reply(const char *kind, const char *fmt, ...)
{
    line_t l = { .len = 0 };
    lp(&l, "%s ", kind);
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(l.buf + l.len, sizeof(l.buf) - l.len, fmt, ap);
    va_end(ap);
    if (n > 0) l.len += n;
    if (l.len >= sizeof(l.buf) - 1) l.len = sizeof(l.buf) - 2;
    l.buf[l.len++] = '\n';
    l.buf[l.len] = '\0';
    fputs(l.buf, stdout);
    fflush(stdout);
}

static const char *uplink_str(void)
{
    if (!settings_is_configured()) return "off";
    switch (modem_ppp_state()) {
    case UPLINK_UP:         return "up";
    case UPLINK_CONNECTING: return "connecting";
    default:                return "down";
    }
}

static void reboot_soon(void)
{
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(300));   // let the reply reach the host
    esp_restart();
}

// ---------------------------------------------------------------------------
//  Commands
// ---------------------------------------------------------------------------
static int cmd_status(int argc, char **argv)
{
    const gateway_settings_t *s = settings_get();
    bool json = (argc == 2 && strcmp(argv[1], "--json") == 0);
    if (argc > 1 && !json) {
        reply("ERROR", "Usage: status [--json]");
        return 0;
    }

    char ip[16] = "";
    modem_ppp_ip_str(ip, sizeof(ip));
    int csq      = modem_ppp_signal_csq();
    int64_t up_s = esp_timer_get_time() / 1000000;

    if (json) {
        static line_t l;
        l.len = 0;
        lp(&l, "STATUS {\"configured\":%s,\"reboot_pending\":%s,\"ssid\":",
           settings_is_configured() ? "true" : "false",
           settings_reboot_pending() ? "true" : "false");
        lp_json_str(&l, s->ssid);
        lp(&l, ",\"open\":%s,\"apn\":", s->password[0] ? "false" : "true");
        lp_json_str(&l, s->apn);
        lp(&l, ",\"apn_user\":");
        lp_json_str(&l, s->apn_user);
        lp(&l, ",\"uplink\":\"%s\",\"signal_csq\":%d,\"ip\":\"%s\",\"clients\":%d,"
               "\"batt_mv\":%d,\"solar_mv\":%d,\"uptime_s\":%lld,\"version\":",
           uplink_str(), csq, ip, wifi_napt_client_count(),
           power_mon_batt_mv(), power_mon_solar_mv(), (long long)up_s);
        lp_json_str(&l, esp_app_get_description()->version);
        lp(&l, "}\n");
        fputs(l.buf, stdout);
        fflush(stdout);
        return 0;
    }

    static line_t l;
    l.len = 0;
    lp(&l, "Setup        : %s\n", settings_is_configured()
           ? "done" : "NEEDED - hotspot and modem are off. Use 'wifi set' and 'apn set', then 'reboot'.");
    lp(&l, "Hotspot      : '%s' (%s), %d device(s) connected\n",
       s->ssid, s->password[0] ? "WPA2" : "open", wifi_napt_client_count());
    lp(&l, "Mobile data  : %s, APN '%s'%s\n", uplink_str(), s->apn,
       s->apn_user[0] ? " (with login)" : "");
    if (csq > 0 && csq != 99) lp(&l, "Signal       : CSQ %d (at last connect; 10+ is usable, 20+ is strong)\n", csq);
    else                      lp(&l, "Signal       : unknown\n");
    if (ip[0])                lp(&l, "Cellular IP  : %s\n", ip);
    lp(&l, "Battery      : %d mV   Solar: %d mV\n", power_mon_batt_mv(), power_mon_solar_mv());
    lp(&l, "Uptime       : %lldh %02lldm\n", (long long)(up_s / 3600), (long long)((up_s / 60) % 60));
    lp(&l, "Firmware     : %s\n", esp_app_get_description()->version);
    if (settings_reboot_pending()) lp(&l, "NOTE         : settings saved - type 'reboot' to apply\n");
    fputs(l.buf, stdout);
    fflush(stdout);
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[1], "set") != 0) {
        reply("ERROR", "Usage: wifi set \"<name>\" \"<password>\"   or   wifi set \"<name>\" --open");
        return 0;
    }
    const char *ssid = argv[2];
    const char *pass = argv[3];
    if (strcmp(pass, "--open") == 0) {
        pass = "";
    } else if (pass[0] == '\0') {
        reply("ERROR", "Password required (8-63 characters), or use --open for a hotspot without password");
        return 0;
    }
    const char *why = settings_check_wifi(ssid, pass);
    if (why) { reply("ERROR", "%s", why); return 0; }

    esp_err_t err = settings_save_wifi(ssid, pass);
    if (err != ESP_OK) { reply("ERROR", "Could not save (%s)", esp_err_to_name(err)); return 0; }
    reply("OK", "Hotspot saved. Type 'reboot' to apply.");
    return 0;
}

static int cmd_apn(int argc, char **argv)
{
    if (argc < 3 || argc > 5 || strcmp(argv[1], "set") != 0) {
        reply("ERROR", "Usage: apn set <apn> [\"<username>\" \"<password>\"]");
        return 0;
    }
    const char *apn  = argv[2];
    const char *user = (argc >= 4) ? argv[3] : "";
    const char *pass = (argc >= 5) ? argv[4] : "";
    const char *why = settings_check_apn(apn, user, pass);
    if (why) { reply("ERROR", "%s", why); return 0; }

    esp_err_t err = settings_save_apn(apn, user, pass);
    if (err != ESP_OK) { reply("ERROR", "Could not save (%s)", esp_err_to_name(err)); return 0; }
    reply("OK", "APN saved. Type 'reboot' to apply.");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    reply("OK", "Rebooting");
    reboot_soon();
    return 0;
}

static int cmd_factory_reset(int argc, char **argv)
{
    esp_err_t err = settings_factory_reset();
    if (err != ESP_OK) { reply("ERROR", "Could not erase (%s)", esp_err_to_name(err)); return 0; }
    reply("OK", "Saved settings erased. Rebooting");
    reboot_soon();
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    const esp_app_desc_t *d = esp_app_get_description();
    reply("OK", "%s (built %s %s, ESP-IDF %s)", d->version, d->date, d->time, d->idf_ver);
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "off") == 0) {
        esp_log_level_set("*", ESP_LOG_NONE);
        reply("OK", "Log messages off (until 'log on' or reboot)");
    } else if (argc == 2 && strcmp(argv[1], "on") == 0) {
        esp_log_level_set("*", ESP_LOG_INFO);
        reply("OK", "Log messages on");
    } else {
        reply("ERROR", "Usage: log on|off");
    }
    return 0;
}

// ---------------------------------------------------------------------------
//  Start
// ---------------------------------------------------------------------------
esp_err_t cli_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt             = "gateway>";
    repl_cfg.max_cmdline_length = 512;    // room for quoted, escaped names/passwords
    repl_cfg.task_stack_size    = 6144;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console init failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_console_cmd_t cmds[] = {
        { .command = "status", .hint = "[--json]", .func = cmd_status,
          .help = "Show setup, hotspot, mobile data, signal and battery." },
        { .command = "wifi", .hint = "set \"<name>\" \"<password>\" | set \"<name>\" --open", .func = cmd_wifi,
          .help = "Save the hotspot name and password (8-63 characters). Applies after 'reboot'." },
        { .command = "apn", .hint = "set <apn> [\"<username>\" \"<password>\"]", .func = cmd_apn,
          .help = "Save the SIM provider's APN, e.g. 'apn set internet.mobiel'. Applies after 'reboot'." },
        { .command = "reboot", .func = cmd_reboot,
          .help = "Restart the board (applies saved settings)." },
        { .command = "factory-reset", .func = cmd_factory_reset,
          .help = "Erase saved settings and restart." },
        { .command = "version", .func = cmd_version,
          .help = "Show the firmware version." },
        { .command = "log", .hint = "on|off", .func = cmd_log,
          .help = "Turn log messages on or off, e.g. to type without interruptions." },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_register_help_command());
    return esp_console_start_repl(repl);
}
