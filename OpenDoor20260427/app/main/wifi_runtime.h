#pragma once

#include <stdbool.h>

#include "freertos/event_groups.h"

#include "esp_err.h"
#include "esp_event.h"

void wifi_runtime_init(EventGroupHandle_t wifiEventGroup);
bool wifi_runtime_has_saved_wifi(void);
esp_err_t wifi_runtime_start_sta_mode(void);
esp_err_t wifi_runtime_start_provision_mode(void);
esp_err_t wifi_runtime_apply_sta_config(const char *ssid, const char *password);
void wifi_runtime_start_button_task(void);
void wifi_runtime_event_handler(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData);
