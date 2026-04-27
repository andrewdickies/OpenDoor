#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef void (*onenet_mqtt_set_door_cb_t)(bool doorStatus);

void onenet_mqtt_set_property_set_callback(onenet_mqtt_set_door_cb_t cb);
void onenet_mqtt_start(void);
void onenet_mqtt_stop(void);
bool onenet_mqtt_is_connected(void);
esp_err_t onenet_mqtt_report_door_status(bool doorStatus, const char *source, int openCount);
