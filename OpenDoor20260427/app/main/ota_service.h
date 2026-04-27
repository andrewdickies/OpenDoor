#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t ota_service_set_url(const char *url);
const char *ota_service_get_url(void);
esp_err_t ota_service_start(void);
int ota_service_build_status_json(char *out, size_t outSize);
