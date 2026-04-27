#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t onenet_token_build(char *outToken, size_t outSize);
