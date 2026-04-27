#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef void (*sta_ws_open_door_cb_t)(const char *source);
typedef void (*sta_ws_close_door_cb_t)(const char *source);
typedef int (*sta_ws_status_json_cb_t)(char *out, size_t outSize);
typedef esp_err_t (*sta_ws_ota_set_url_cb_t)(const char *url);
typedef esp_err_t (*sta_ws_ota_start_cb_t)(void);
typedef int (*sta_ws_ota_status_json_cb_t)(char *out, size_t outSize);

esp_err_t sta_ws_server_start(
	sta_ws_open_door_cb_t openDoorCb,
	sta_ws_close_door_cb_t closeDoorCb,
	sta_ws_status_json_cb_t statusJsonCb,
	sta_ws_ota_set_url_cb_t otaSetUrlCb,
	sta_ws_ota_start_cb_t otaStartCb,
	sta_ws_ota_status_json_cb_t otaStatusCb);
void sta_ws_server_stop(void);
