#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include "app_settings.h"
#include "door_control.h"
#include "onenet_mqtt.h"
#include "servo_control.h"
#include "wifi_runtime.h"

static EventGroupHandle_t s_wifiEventGroup;

static void on_onenet_property_set(bool doorStatus)
{
	if (doorStatus) {
		door_control_open("onenet_set");
	} else {
		door_control_close("onenet_set");
	}
}

static void on_door_state_report(const char *source, bool doorStatus, int openCount)
{
	if (onenet_mqtt_report_door_status(doorStatus, source, openCount) != ESP_OK) {
		ESP_LOGW("app_main", "OneNET report skipped (not connected)");
	}
}

void app_main(void)
{
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init_config_t wifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifiInitCfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

	s_wifiEventGroup = xEventGroupCreate();
	wifi_runtime_init(s_wifiEventGroup);

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_runtime_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_runtime_event_handler, NULL));

	gpio_config_t ioCfg;
	memset(&ioCfg, 0, sizeof(ioCfg));
	ioCfg.pin_bit_mask = 1ULL << app_settings_get_boot_gpio();
	ioCfg.mode = GPIO_MODE_INPUT;
	ioCfg.pull_up_en = 1;
	ioCfg.pull_down_en = 0;
	ioCfg.intr_type = GPIO_INTR_DISABLE;
	ESP_ERROR_CHECK(gpio_config(&ioCfg));
	ESP_ERROR_CHECK(servo_control_init());
	onenet_mqtt_set_property_set_callback(on_onenet_property_set);
	door_control_set_state_callback(on_door_state_report);

	wifi_runtime_start_button_task();

	if (wifi_runtime_has_saved_wifi()) {
		wifi_runtime_start_sta_mode();
	} else {
		wifi_runtime_start_provision_mode();
	}
}
