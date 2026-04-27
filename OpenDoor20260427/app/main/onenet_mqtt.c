#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "config.h"
#include "onenet_mqtt.h"
#include "onenet_token.h"

static const char *TAG = "onenet_mqtt";
static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static bool s_started;
static char s_token[512];
static char s_mqttUri[128];
static uint32_t s_msgSeq = 1;
static bool s_lastDoorStatus;
static onenet_mqtt_set_door_cb_t s_setDoorCb;

static void onenet_handle_property_set(const char *data, int dataLen);
static void onenet_handle_property_get(const char *data, int dataLen);

void onenet_mqtt_set_property_set_callback(onenet_mqtt_set_door_cb_t cb)
{
	s_setDoorCb = cb;
}

static esp_err_t onenet_publish_property(bool doorStatus, const char *source, int openCount)
{
	char payload[256];
	int msgId;

	if (!s_client || !s_connected) {
		return ESP_ERR_INVALID_STATE;
	}
	snprintf(payload, sizeof(payload),
		"{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"door_status\":{\"value\":%s}},\"method\":\"thing.property.post\"}",
		(unsigned long) s_msgSeq++, doorStatus ? "true" : "false");
	msgId = esp_mqtt_client_publish(s_client, APP_ONENET_PUB_TOPIC, payload, 0, 1, 0);
	ESP_LOGI(TAG, "publish msgId=%d door_status=%d source=%s openCount=%d", msgId, doorStatus ? 1 : 0, source, openCount);
	if (msgId < 0) {
		return ESP_FAIL;
	}
	return ESP_OK;
}

static void onenet_publish_set_reply(const char *id, int code, const char *msg)
{
	char payload[192];
	if (!s_client || !s_connected) {
		return;
	}
	snprintf(payload, sizeof(payload),
		"{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\"}",
		id ? id : "0", code, msg ? msg : "");
	esp_mqtt_client_publish(s_client, APP_ONENET_PUB_SET_REPLY_TOPIC, payload, 0, 1, 0);
}

static void onenet_publish_get_reply(const char *id, int code, const char *msg, bool doorStatus)
{
	char payload[224];
	if (!s_client || !s_connected) {
		return;
	}
	snprintf(payload, sizeof(payload),
		"{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\",\"data\":{\"door_status\":%s}}",
		id ? id : "0", code, msg ? msg : "", doorStatus ? "true" : "false");
	esp_mqtt_client_publish(s_client, APP_ONENET_PUB_GET_REPLY_TOPIC, payload, 0, 1, 0);
}

static void json_extract_id(const char *data, int dataLen, char *out, size_t outSize)
{
	const char *idPos;
	const char *start;
	const char *end;

	strlcpy(out, "0", outSize);
	idPos = strstr(data, "\"id\"");
	if (!idPos || idPos >= data + dataLen) {
		return;
	}
	start = strchr(idPos, ':');
	if (!start || start >= data + dataLen) {
		return;
	}
	start++;
	while (*start == ' ' || *start == '\t' || *start == '\"') {
		start++;
	}
	end = start;
	while (end < data + dataLen && *end != ',' && *end != '}' && *end != '\"' && *end != '\r' && *end != '\n') {
		end++;
	}
	if (end <= start || (size_t) (end - start) >= outSize) {
		return;
	}
	memcpy(out, start, (size_t) (end - start));
	out[end - start] = '\0';
}

static void onenet_handle_property_set(const char *data, int dataLen)
{
	char idBuf[24];
	bool doorStatus;

	if (!data || dataLen <= 0) {
		return;
	}
	json_extract_id(data, dataLen, idBuf, sizeof(idBuf));

	if (strstr(data, "\"door_status\":{\"value\":true") || strstr(data, "\"door_status\":true")) {
		doorStatus = true;
	} else if (strstr(data, "\"door_status\":{\"value\":false") || strstr(data, "\"door_status\":false")) {
		doorStatus = false;
	} else {
		onenet_publish_set_reply(idBuf, 400, "door_status missing");
		return;
	}

	if (s_setDoorCb) {
		s_setDoorCb(doorStatus);
		onenet_publish_set_reply(idBuf, 200, "success");
	} else {
		onenet_publish_set_reply(idBuf, 500, "device callback missing");
	}
}

static void onenet_handle_property_get(const char *data, int dataLen)
{
	char idBuf[24];
	if (!data || dataLen <= 0) {
		return;
	}
	json_extract_id(data, dataLen, idBuf, sizeof(idBuf));
	onenet_publish_get_reply(idBuf, 200, "success", s_lastDoorStatus);
	(void) onenet_publish_property(s_lastDoorStatus, "onenet_get", 0);
}

static esp_err_t onenet_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	switch (event->event_id) {
	case MQTT_EVENT_CONNECTED:
		s_connected = true;
		ESP_LOGI(TAG, "OneNET MQTT connected");
		esp_mqtt_client_subscribe(s_client, APP_ONENET_PUB_REPLY_TOPIC, 0);
		esp_mqtt_client_subscribe(s_client, APP_ONENET_SUB_SET_TOPIC, 0);
		esp_mqtt_client_subscribe(s_client, APP_ONENET_SUB_GET_TOPIC, 0);
		(void) onenet_publish_property(s_lastDoorStatus, "boot", 0);
		break;
	case MQTT_EVENT_DISCONNECTED:
		s_connected = false;
		s_started = false;
		ESP_LOGW(TAG, "OneNET MQTT disconnected");
		break;
	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "reply topic=%.*s data=%.*s",
			event->topic_len, event->topic, event->data_len, event->data);
		if (event->topic_len == (int) strlen(APP_ONENET_SUB_SET_TOPIC)
			&& strncmp(event->topic, APP_ONENET_SUB_SET_TOPIC, event->topic_len) == 0) {
			onenet_handle_property_set(event->data, event->data_len);
		} else if (event->topic_len == (int) strlen(APP_ONENET_SUB_GET_TOPIC)
			&& strncmp(event->topic, APP_ONENET_SUB_GET_TOPIC, event->topic_len) == 0) {
			onenet_handle_property_get(event->data, event->data_len);
		}
		break;
	default:
		break;
	}
	return ESP_OK;
}

void onenet_mqtt_start(void)
{
	esp_mqtt_client_config_t cfg;
	esp_err_t err;

	if (s_client) {
		if (!s_started) {
			err = esp_mqtt_client_start(s_client);
			if (err == ESP_OK) {
				s_started = true;
			} else {
				ESP_LOGE(TAG, "mqtt restart failed err=%d", err);
			}
		}
		return;
	}
	memset(&cfg, 0, sizeof(cfg));
	ESP_LOGW(TAG, "building token for mqtt connect...");
	if (onenet_token_build(s_token, sizeof(s_token)) != ESP_OK) {
		ESP_LOGE(TAG, "build token failed");
		return;
	}
	snprintf(s_mqttUri, sizeof(s_mqttUri), "mqtt://%s:%d", APP_ONENET_MQTT_HOST, APP_ONENET_MQTT_PORT);
	cfg.uri = s_mqttUri;
	cfg.client_id = APP_ONENET_DEVICE_NAME;
	cfg.username = APP_ONENET_PRODUCT_ID;
	cfg.password = s_token;
	cfg.event_handle = onenet_mqtt_event_handler;

	s_client = esp_mqtt_client_init(&cfg);
	if (!s_client) {
		ESP_LOGE(TAG, "mqtt init failed");
		return;
	}
	if (esp_mqtt_client_start(s_client) == ESP_OK) {
		s_started = true;
	} else {
		ESP_LOGE(TAG, "mqtt start failed");
	}
}

void onenet_mqtt_stop(void)
{
	if (!s_client) {
		return;
	}
	if (s_started) {
		esp_mqtt_client_stop(s_client);
	}
	esp_mqtt_client_destroy(s_client);
	s_client = NULL;
	s_connected = false;
	s_started = false;
}

bool onenet_mqtt_is_connected(void)
{
	return s_connected;
}

esp_err_t onenet_mqtt_report_door_status(bool doorStatus, const char *source, int openCount)
{
	s_lastDoorStatus = doorStatus;
	return onenet_publish_property(doorStatus, source ? source : "unknown", openCount);
}
