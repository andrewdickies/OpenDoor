#include <string.h>

#include "esp_wifi.h"

#include "app_settings.h"
#include "config.h"

int app_settings_get_boot_gpio(void)
{
	return APP_BOOT_GPIO;
}

int app_settings_get_short_press_min_ms(void)
{
	return APP_SHORT_PRESS_MIN_MS;
}

int app_settings_get_long_press_ms(void)
{
	return APP_LONG_PRESS_MS;
}

const char *app_settings_get_prov_ap_ssid(void)
{
	return APP_PROV_AP_SSID;
}

const char *app_settings_get_prov_ap_password(void)
{
	return APP_PROV_AP_PASSWORD;
}

void app_settings_build_ap_config(wifi_config_t *apCfg)
{
	memset(apCfg, 0, sizeof(*apCfg));
	strlcpy((char *) apCfg->ap.ssid, app_settings_get_prov_ap_ssid(), sizeof(apCfg->ap.ssid));
	strlcpy((char *) apCfg->ap.password, app_settings_get_prov_ap_password(), sizeof(apCfg->ap.password));
	apCfg->ap.max_connection = 4;

	if (strlen(app_settings_get_prov_ap_password()) == 0) {
		apCfg->ap.authmode = WIFI_AUTH_OPEN;
	} else {
		apCfg->ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
	}
}
