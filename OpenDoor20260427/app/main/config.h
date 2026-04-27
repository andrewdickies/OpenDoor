#pragma once

#define APP_BOOT_GPIO				0
#define APP_SHORT_PRESS_MIN_MS		80
#define APP_LONG_PRESS_MS			3000

#define APP_PROV_AP_SSID			"OpenDoor-Provision"
#define APP_PROV_AP_PASSWORD		"12345678"

#define APP_HTTP_HEADER_MAX_LEN		2048

#define APP_MDNS_HOSTNAME			"opendoor"
#define APP_MDNS_INSTANCE			"OpenDoor Device"

#define APP_OTA_DEFAULT_URL			"http://192.168.3.101/esp8266/app.bin"

#define APP_SERVO_GPIO				2
#define APP_SERVO_MIN_PULSE_US		500
#define APP_SERVO_MAX_PULSE_US		2500
#define APP_SERVO_OPEN_ANGLE		90
#define APP_SERVO_CLOSE_ANGLE		0
#define APP_DOOR_AUTO_CLOSE_ENABLE	1
#define APP_DOOR_AUTO_CLOSE_DELAY_MS	3000

#define APP_ONENET_MQTT_HOST		"mqtts.heclouds.com"
#define APP_ONENET_MQTT_PORT		1883
#define APP_ONENET_PRODUCT_ID		"15J1DTPOaS"
#define APP_ONENET_DEVICE_NAME		"open_door_01"
#define APP_ONENET_DEVICE_KEY		"92PHeyv5F4sH41lH4Td7RQ8H26/XEY8xqXcFG9e12+U="
#define APP_ONENET_TOKEN_VERSION	"2018-10-31"
#define APP_ONENET_TOKEN_METHOD		"sha256"
#define APP_ONENET_TOKEN_EXPIRE_YEARS	5
#define APP_ONENET_PUB_TOPIC		"$sys/15J1DTPOaS/open_door_01/thing/property/post"
#define APP_ONENET_PUB_REPLY_TOPIC	"$sys/15J1DTPOaS/open_door_01/thing/property/post/reply"
#define APP_ONENET_SUB_SET_TOPIC	"$sys/15J1DTPOaS/open_door_01/thing/property/set"
#define APP_ONENET_PUB_SET_REPLY_TOPIC "$sys/15J1DTPOaS/open_door_01/thing/property/set_reply"
#define APP_ONENET_SUB_GET_TOPIC	"$sys/15J1DTPOaS/open_door_01/thing/property/get"
#define APP_ONENET_PUB_GET_REPLY_TOPIC "$sys/15J1DTPOaS/open_door_01/thing/property/get_reply"
