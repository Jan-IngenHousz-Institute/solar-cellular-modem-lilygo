idf_component_register(
    SRCS
        "main.c"
        "modem_ppp.c"
        "wifi_napt.c"
        "power_mon.c"
        "settings.c"
        "cli.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_adc
        driver
        lwip
        esp_modem
        esp_timer
        esp_hw_support
        esp_system
        console
        esp_app_format
)

# lwIP's NAT forgets a UDP or ICMP flow 2 s after its last packet. On a cellular
# link a DNS or NTP reply regularly takes longer than that under load; a reply
# with no NAT entry is dropped, so devices behind the gateway see lookups fail
# and cannot connect. Keep the entries for as long as a reply is still useful.
idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PRIVATE
    IP_NAPT_TIMEOUT_MS_UDP=30000
    IP_NAPT_TIMEOUT_MS_ICMP=10000)
