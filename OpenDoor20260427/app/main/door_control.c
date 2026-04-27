#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"

#include "config.h"
#include "door_control.h"
#include "servo_control.h"

static const char *TAG = "door_control";
static int s_openCount;
static int s_closeCount;
static bool s_isOpen;
static char s_lastSource[32] = "none";
static uint32_t s_lastOpenMs;
static door_control_state_cb_t s_stateCb;
static TimerHandle_t s_autoCloseTimer;

void door_control_close(const char *source);

static void door_control_auto_close_cb(TimerHandle_t timer)
{
	(void) timer;
	if (s_isOpen) {
		door_control_close("auto_close");
	}
}

static void door_control_schedule_auto_close(void)
{
#if APP_DOOR_AUTO_CLOSE_ENABLE
	if (!s_autoCloseTimer) {
		s_autoCloseTimer = xTimerCreate("door_auto_close",
			pdMS_TO_TICKS(APP_DOOR_AUTO_CLOSE_DELAY_MS), pdFALSE, NULL, door_control_auto_close_cb);
	}
	if (s_autoCloseTimer) {
		xTimerStop(s_autoCloseTimer, 0);
		xTimerChangePeriod(s_autoCloseTimer, pdMS_TO_TICKS(APP_DOOR_AUTO_CLOSE_DELAY_MS), 0);
		xTimerStart(s_autoCloseTimer, 0);
	}
#endif
}

static void door_control_cancel_auto_close(void)
{
#if APP_DOOR_AUTO_CLOSE_ENABLE
	if (s_autoCloseTimer) {
		xTimerStop(s_autoCloseTimer, 0);
	}
#endif
}

static void door_control_notify_state(const char *source)
{
	s_lastOpenMs = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
	strlcpy(s_lastSource, source ? source : "unknown", sizeof(s_lastSource));
	if (s_stateCb) {
		s_stateCb(s_lastSource, s_isOpen, s_openCount);
	}
}

void door_control_set_state_callback(door_control_state_cb_t cb)
{
	s_stateCb = cb;
}

void door_control_open(const char *source)
{
	s_openCount++;
	s_isOpen = true;
	door_control_notify_state(source);
	ESP_LOGI(TAG, "DOOR OPEN source=%s count=%d", s_lastSource, s_openCount);
	servo_control_set_angle(APP_SERVO_OPEN_ANGLE);
	door_control_schedule_auto_close();
}

void door_control_close(const char *source)
{
	door_control_cancel_auto_close();
	s_closeCount++;
	s_isOpen = false;
	door_control_notify_state(source);
	ESP_LOGI(TAG, "DOOR CLOSE source=%s count=%d", s_lastSource, s_closeCount);
	servo_control_set_angle(APP_SERVO_CLOSE_ANGLE);
}

int door_control_build_status_json(char *out, size_t outSize)
{
	return snprintf(out, outSize,
		"{\"doorOpen\":%s,\"openCount\":%d,\"closeCount\":%d,\"lastSource\":\"%s\",\"lastActionMs\":%lu}",
		s_isOpen ? "true" : "false", s_openCount, s_closeCount, s_lastSource, (unsigned long) s_lastOpenMs);
}
