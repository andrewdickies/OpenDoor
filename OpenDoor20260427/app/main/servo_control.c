#include <stdint.h>
#include <stdbool.h>

#include "driver/pwm.h"
#include "esp_log.h"

#include "config.h"
#include "servo_control.h"

static const char *TAG = "servo_control";
static bool s_inited;

static uint32_t servo_angle_to_pulse_us(uint16_t angle)
{
	uint32_t minPulse = APP_SERVO_MIN_PULSE_US;
	uint32_t maxPulse = APP_SERVO_MAX_PULSE_US;
	uint32_t span;

	if (angle > 180) {
		angle = 180;
	}
	if (maxPulse <= minPulse) {
		return minPulse;
	}
	span = maxPulse - minPulse;
	return minPulse + (span * angle) / 180;
}

esp_err_t servo_control_init(void)
{
	uint32_t duties[1];
	uint32_t pins[1];
	esp_err_t err;

	if (s_inited) {
		return ESP_OK;
	}

	duties[0] = servo_angle_to_pulse_us(APP_SERVO_CLOSE_ANGLE);
	pins[0] = APP_SERVO_GPIO;

	err = pwm_init(20000, duties, 1, pins);
	if (err != ESP_OK) {
		return err;
	}
	err = pwm_set_phase(0, 0);
	if (err != ESP_OK) {
		return err;
	}
	err = pwm_start();
	if (err != ESP_OK) {
		return err;
	}

	s_inited = true;
	ESP_LOGI(TAG, "Servo PWM init gpio=%d", APP_SERVO_GPIO);
	return ESP_OK;
}

esp_err_t servo_control_set_angle(uint16_t angle)
{
	uint32_t duty;
	esp_err_t err;

	if (!s_inited) {
		err = servo_control_init();
		if (err != ESP_OK) {
			return err;
		}
	}

	duty = servo_angle_to_pulse_us(angle);
	err = pwm_set_duty(0, duty);
	if (err != ESP_OK) {
		return err;
	}
	err = pwm_start();
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "Servo angle=%u duty=%luus", angle > 180 ? 180 : angle, (unsigned long) duty);
	}
	return err;
}
