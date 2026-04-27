#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_wifi.h"

#include "app_settings.h"
#include "boot_button.h"
#include "door_control.h"
#include "mdns_service.h"
#include "onenet_mqtt.h"
#include "ota_service.h"
#include "provisioning_web.h"
#include "sta_ws_server.h"
#include "time_sync.h"
#include "wifi_runtime.h"

static const char *TAG = "wifi_runtime";

static EventGroupHandle_t s_wifiEventGroup;
static bool s_inProvisionMode;
static bool s_staServicesStarted;
static volatile bool s_staServiceStartPending;
static TaskHandle_t s_staServiceStartTask;

static void door_open_adapter(const char *source);
static void door_close_adapter(const char *source);
static esp_err_t ota_start_adapter(void);

static void wifi_stop_safe(void)
{
	esp_err_t err = esp_wifi_stop();
	if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
		ESP_ERROR_CHECK(err);
	}
}

static void sta_service_start_task(void *arg)
{
	(void) arg;
	s_staServiceStartTask = xTaskGetCurrentTaskHandle();
	time_sync_start_once();
	if (!s_inProvisionMode && !s_staServicesStarted) {
		if (mdns_service_start() == ESP_OK) {
			onenet_mqtt_start();
			(void) sta_ws_server_start(
				door_open_adapter,
				door_close_adapter,
				door_control_build_status_json,
				ota_service_set_url,
				ota_start_adapter,
				ota_service_build_status_json);
			s_staServicesStarted = true;
		} else {
			ESP_LOGE(TAG, "STA service start failed");
		}
	}
	s_staServiceStartPending = false;
	s_staServiceStartTask = NULL;
	vTaskDelete(NULL);
}

static void door_open_adapter(const char *source)
{
	door_control_open(source);
}

static void door_close_adapter(const char *source)
{
	door_control_close(source);
}

static esp_err_t ota_start_adapter(void)
{
	return ota_service_start();
}

void wifi_runtime_init(EventGroupHandle_t wifiEventGroup)
{
	s_wifiEventGroup = wifiEventGroup;
	s_inProvisionMode = false;
	s_staServicesStarted = false;
	s_staServiceStartPending = false;
	s_staServiceStartTask = NULL;
}

bool wifi_runtime_has_saved_wifi(void)
{
	wifi_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	if (esp_wifi_get_config(ESP_IF_WIFI_STA, &cfg) != ESP_OK) {
		return false;
	}
	return strlen((const char *) cfg.sta.ssid) > 0;
}

esp_err_t wifi_runtime_apply_sta_config(const char *ssid, const char *password)
{
	wifi_config_t wifiCfg;
	memset(&wifiCfg, 0, sizeof(wifiCfg));
	strlcpy((char *) wifiCfg.sta.ssid, ssid, sizeof(wifiCfg.sta.ssid));
	strlcpy((char *) wifiCfg.sta.password, password, sizeof(wifiCfg.sta.password));
	wifiCfg.sta.bssid_set = 0;
	wifiCfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
	wifiCfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
	wifiCfg.sta.threshold.rssi = -127;
	if (strlen(password) >= 8) {
		/* Compatibility mode: allow WPA/WPA2 mixed APs. */
		wifiCfg.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifiCfg));
	ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT20));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
	ESP_ERROR_CHECK(esp_wifi_connect());
	return ESP_OK;
}

esp_err_t wifi_runtime_start_sta_mode(void)
{
	ESP_LOGI(TAG, "Starting STA mode");
	s_inProvisionMode = false;
	provisioning_web_stop();
	sta_ws_server_stop();
	mdns_service_stop();
	onenet_mqtt_stop();
	s_staServicesStarted = false;
	s_staServiceStartPending = false;
	if (s_staServiceStartTask) {
		vTaskDelete(s_staServiceStartTask);
		s_staServiceStartTask = NULL;
	}

	wifi_stop_safe();
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT20));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
	ESP_ERROR_CHECK(esp_wifi_connect());
	return ESP_OK;
}

esp_err_t wifi_runtime_start_provision_mode(void)
{
	wifi_config_t apCfg;
	esp_err_t err;
	wifi_config_t emptyStaCfg;
	int retry;

	ESP_LOGI(TAG, "Starting provisioning AP: %s", app_settings_get_prov_ap_ssid());
	s_inProvisionMode = true;
	sta_ws_server_stop();
	if (s_staServiceStartTask) {
		vTaskDelete(s_staServiceStartTask);
		s_staServiceStartTask = NULL;
	}
	/* Give STA web server time to release :80 before provisioning HTTPD binds it. */
	vTaskDelay(pdMS_TO_TICKS(300));
	mdns_service_stop();
	onenet_mqtt_stop();
	s_staServicesStarted = false;
	s_staServiceStartPending = false;

	app_settings_build_ap_config(&apCfg);
	memset(&emptyStaCfg, 0, sizeof(emptyStaCfg));

	err = esp_wifi_disconnect();
	if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
		ESP_LOGW(TAG, "wifi disconnect ignored err=%d", err);
	}
	wifi_stop_safe();
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &emptyStaCfg));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &apCfg));
	ESP_ERROR_CHECK(esp_wifi_start());

	err = ESP_FAIL;
	for (retry = 0; retry < 5; retry++) {
		err = provisioning_web_start(wifi_runtime_apply_sta_config);
		if (err == ESP_OK) {
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(200));
	}
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "start provisioning web failed err=%d after retry", err);
		return err;
	}

	ESP_LOGI(TAG, "Connect AP and open: http://192.168.4.1/");
	return ESP_OK;
}

void wifi_runtime_event_handler(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData)
{
	if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
		return;
	}

	if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
		if (eventData) {
			wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *) eventData;
			ESP_LOGW(TAG, "STA disconnected, reason=%d", (int) disc->reason);
		} else {
			ESP_LOGW(TAG, "STA disconnected, reason=unknown");
		}
		if (!s_inProvisionMode) {
			sta_ws_server_stop();
			mdns_service_stop();
			onenet_mqtt_stop();
			s_staServicesStarted = false;
			s_staServiceStartPending = false;
			if (s_staServiceStartTask) {
				vTaskDelete(s_staServiceStartTask);
				s_staServiceStartTask = NULL;
			}
		}
		if (!s_inProvisionMode) {
			esp_wifi_connect();
		}
		return;
	}

	if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *evt = (ip_event_got_ip_t *) eventData;
		ESP_LOGI(TAG, "STA got ip:" IPSTR, IP2STR(&evt->ip_info.ip));
		xEventGroupSetBits(s_wifiEventGroup, BIT0);
		if (s_inProvisionMode) {
			ESP_LOGI(TAG, "Provision success, switch to STA only");
			provisioning_web_stop();
			s_inProvisionMode = false;
			wifi_stop_safe();
			ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
			ESP_ERROR_CHECK(esp_wifi_start());
			ESP_ERROR_CHECK(esp_wifi_connect());
		}
		if (!s_staServicesStarted && !s_staServiceStartPending) {
			s_staServiceStartPending = true;
			if (xTaskCreate(sta_service_start_task, "sta_srv_start", 4096, NULL, 4, &s_staServiceStartTask) != pdPASS) {
				s_staServiceStartPending = false;
				s_staServiceStartTask = NULL;
				ESP_LOGE(TAG, "start sta service task failed");
			}
		}
	}
}

static void on_boot_short_press(void)
{
	door_control_open("boot_short");
}

static void on_boot_long_press(void)
{
	ESP_LOGI(TAG, "BOOT long pressed, enter provisioning");
	wifi_runtime_start_provision_mode();
}

void wifi_runtime_start_button_task(void)
{
	boot_button_start(on_boot_short_press, on_boot_long_press);
}
