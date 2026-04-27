#pragma once

#include "esp_wifi.h"

int app_settings_get_boot_gpio(void);
int app_settings_get_short_press_min_ms(void);
int app_settings_get_long_press_ms(void);
const char *app_settings_get_prov_ap_ssid(void);
const char *app_settings_get_prov_ap_password(void);
void app_settings_build_ap_config(wifi_config_t *apCfg);
