#pragma once

#include "esp_err.h"

typedef esp_err_t (*provisioning_apply_cb_t)(const char *ssid, const char *password);

esp_err_t provisioning_web_start(provisioning_apply_cb_t applyCb);
void provisioning_web_stop(void);
