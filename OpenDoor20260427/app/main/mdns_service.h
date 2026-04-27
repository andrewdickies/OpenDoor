#pragma once

#include "esp_err.h"

esp_err_t mdns_service_start(void);
void mdns_service_stop(void);
