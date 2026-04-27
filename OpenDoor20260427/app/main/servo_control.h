#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t servo_control_init(void);
esp_err_t servo_control_set_angle(uint16_t angle);
