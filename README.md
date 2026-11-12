# T-SIM7000G — Transparent Cellular → WiFi NAT Router

Firmware for a **LilyGo T-SIM7000G** (ESP32-WROVER + SIM7000G LTE-M/NB-IoT/GPRS, battery + solar) that:

1. Brings up a **WiFi hotspot** (fixed SSID/password), and
2. **Shares the SIM card's cellular internet** to the devices that join it — by routing their traffic over the modem with **NAT (NAPT)**.


Built with **PlatformIO + ESP-IDF** (not Arduino: lwIP NAPT is reliable on IDF and broken on the stock Arduino core).

---

## 1. Configure

**Credentials** stay out of git. Copy [`src/app_secrets.example.h`](src/app_secrets.example.h) to `src/app_secrets.h` (gitignored) and set:

| Setting | What |
|---|---|
| `WIFI_AP_SSID` / `WIFI_AP_PASSWORD` | the hotspot name + password (≥8 chars for WPA2, or `""` for an open AP) |
| `CELL_APN_USER` / `CELL_APN_PASS` | only if your SIM needs them (usually empty) |

**Device settings** are in [`src/app_config.h`](src/app_config.h):

| Setting | What |
|---|---|
| `CELL_APN` | **REQUIRED** — your SIM carrier's APN. Wrong APN = no data. |
| `BATT_LOW_MV` | low-battery deep-sleep cutoff in mV; set **`0` to disable** (always run while powered) |

Board pins are pre-filled and verified — don't change the `PIN_*` section for this board.

## 2. Build & flash

```bash
pio run                      # first build downloads the IDF toolchain + esp_modem (large/slow)
pio run -t upload -t monitor # flash + serial monitor @ 115200
```

Expected boot log:

```
main:   ===== T-SIM7000G cellular->WiFi NAT router =====
wifiap: SoftAP 'AmbitGateway' up (WPA2-PSK), max 10 clients
modem:  PWRKEY pulse 1/2
modem:  network registered (CSQ rssi=17)
modem:  switching to DATA (PPP) mode, APN='internet'
modem:  PPP GOT IP 10.x.x.x gw 10.x.x.x
wifiap: NAPT enabled on SoftAP — clients now have internet
modem:  UPLINK UP — routing clients over cellular
```

## 3. Status LED (GPIO12)

| Pattern | Meaning |
|---|---|
| Fast blink | modem down / booting / unresponsive |
| Slow blink | registered or dialing, no IP yet |
| **Solid on** | **online — routing clients** |
| Double-blink | low battery (about to deep-sleep) |

## 4. Verify end-to-end

1. Join SSID `AmbitGateway` from a phone/laptop → you should get a `192.168.4.x` lease.
2. Once the LED is solid, browse the web / `ping 8.8.8.8` → traffic is NAT-routed over cellular.
3. Point one ambyte at the hotspot → confirm its MQTT-over-TLS to openJII connects.
4. Resilience: pull the LTE antenna or drop the network → LED returns to the connecting pattern and recovers **without rebooting**; the AP stays joinable throughout.

---

## Deployment notes

- **Capacity:** the ESP32 SoftAP caps at **10 simultaneously-associated** clients (hard chip limit). For **~10–25 sleepy nodes** (wake → publish → sleep), one gateway is plenty — only ~1–2 are ever joined at once.
- **Multiple gateways:** flash this **same binary** onto several boards, each with its **own `WIFI_AP_SSID`**. Do this for WiFi **coverage** (nodes spread out) or **redundancy** — not for capacity.
- **The real shared limit is the cellular uplink** (LTE-M ≤ ~300 kbps, high-latency), not client count. Site placement / signal matters most.


---

## Troubleshooting

- **Modem never registers / no `PPP GOT IP`:** wrong `CELL_APN` (most common); weak signal (check the LTE antenna + SIM seating); SIM not data-enabled or PIN-locked.
- **Modem never responds to AT (stays "fast blink"):** PWRKEY polarity — flip `PWRKEY_ASSERT` in [`src/modem_ppp.c`](src/modem_ppp.c).
- **`esp_netif_napt_enable failed`:** `CONFIG_LWIP_IPV4_NAPT` not set — confirm `sdkconfig.defaults` was applied (delete the generated `sdkconfig` and rebuild).
- **Random resets when the modem transmits:** weak power/battery sag. Brownout is already set to the most tolerant level; use a healthy battery and good USB/solar supply.
- **PlatformIO pulls an old ESP-IDF (no `esp_netif_napt_enable`):** ensure `platform = espressif32 @ ^6.9.0` (ESP-IDF 5.1+). If still stuck, switch to the `pioarduino` platform fork in `platformio.ini`.
- **`esp_modem` won't resolve:** bump `espressif/esp_modem` in [`src/idf_component.yml`](src/idf_component.yml) (try `"^2"` or `"*"`).

## What this firmware does NOT do (v1)
No MQTT/TLS on the gateway (end-to-end on the ambytes by design), no GPS, no SD logging, no OTA, no web UI. All are clean follow-ups.
