#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "app_settings.h"
#include "boot_button.h"

static const char *TAG = "boot_button";
static boot_button_press_cb_t s_onShortPress;
static boot_button_press_cb_t s_onLongPress;

static void boot_button_task(void *arg)
{
	const TickType_t pollMs = pdMS_TO_TICKS(50);
	const TickType_t shortPressTicks = pdMS_TO_TICKS(app_settings_get_short_press_min_ms());
	const TickType_t longPressTicks = pdMS_TO_TICKS(app_settings_get_long_press_ms());
	TickType_t pressStartTick = 0;
	bool longReported = false;
	int bootGpio = app_settings_get_boot_gpio();

	while (1) {
		int level = gpio_get_level(bootGpio);
		if (level == 0) {
			if (pressStartTick == 0) {
				pressStartTick = xTaskGetTickCount();
			}
			if (!longReported && (xTaskGetTickCount() - pressStartTick) >= longPressTicks) {
				longReported = true;
				ESP_LOGI(TAG, "BOOT long pressed");
				if (s_onLongPress) {
					s_onLongPress();
				}
			}
		} else {
			TickType_t pressedTicks = (pressStartTick > 0) ? (xTaskGetTickCount() - pressStartTick) : 0;
			if (!longReported && pressedTicks >= shortPressTicks) {
				ESP_LOGI(TAG, "BOOT short pressed");
				if (s_onShortPress) {
					s_onShortPress();
				}
			}
			pressStartTick = 0;
			longReported = false;
		}
		vTaskDelay(pollMs);
	}
}

void boot_button_start(boot_button_press_cb_t onShortPress, boot_button_press_cb_t onLongPress)
{
	s_onShortPress = onShortPress;
	s_onLongPress = onLongPress;
	xTaskCreate(boot_button_task, "boot_btn_task", 2048, NULL, 5, NULL);
}
