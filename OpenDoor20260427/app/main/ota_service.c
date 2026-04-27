#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "config.h"
#include "ota_service.h"

typedef enum {
	OTA_STATE_IDLE = 0,
	OTA_STATE_RUNNING,
	OTA_STATE_SUCCESS,
	OTA_STATE_FAILED
} ota_state_t;

static const char *TAG = "ota_service";

static char s_otaUrl[256] = APP_OTA_DEFAULT_URL;
static ota_state_t s_otaState = OTA_STATE_IDLE;
static int s_otaLastErr;
static int s_otaProgress;
static int s_otaTotalBytes;
static int s_otaWrittenBytes;
static int s_otaSpeedBps;
static int s_otaElapsedMs;
static TaskHandle_t s_otaTask;

static bool ota_url_valid(const char *url)
{
	return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

esp_err_t ota_service_set_url(const char *url)
{
	if (!ota_url_valid(url)) {
		return ESP_ERR_INVALID_ARG;
	}
	if (strlen(url) >= sizeof(s_otaUrl)) {
		return ESP_ERR_INVALID_SIZE;
	}
	strlcpy(s_otaUrl, url, sizeof(s_otaUrl));
	ESP_LOGI(TAG, "OTA URL updated: %s", s_otaUrl);
	return ESP_OK;
}

const char *ota_service_get_url(void)
{
	return s_otaUrl;
}

static void ota_task(void *arg)
{
	esp_http_client_config_t httpCfg = {
		.timeout_ms = 30000
	};
	esp_http_client_handle_t client = NULL;
	const esp_partition_t *updatePartition = NULL;
	esp_ota_handle_t otaHandle = 0;
	esp_err_t err;
	char buf[1024];
	int httpsRetryCount = 0;
	bool retryHttps = false;
	char activeUrl[256];
	TickType_t startTick = 0;

	strlcpy(activeUrl, s_otaUrl, sizeof(activeUrl));

	s_otaState = OTA_STATE_RUNNING;
	s_otaLastErr = 0;
	s_otaProgress = 0;
	s_otaWrittenBytes = 0;
	s_otaTotalBytes = -1;
	s_otaSpeedBps = 0;
	s_otaElapsedMs = 0;
	ESP_LOGI(TAG, "OTA start from URL: %s", s_otaUrl);

	updatePartition = esp_ota_get_next_update_partition(NULL);
	if (!updatePartition) {
		s_otaState = OTA_STATE_FAILED;
		s_otaLastErr = ESP_ERR_NOT_FOUND;
		goto done;
	}

retry_with_url:
	otaHandle = 0;
	s_otaState = OTA_STATE_RUNNING;
	s_otaLastErr = 0;
	s_otaProgress = 0;
	s_otaWrittenBytes = 0;
	s_otaTotalBytes = -1;
	s_otaSpeedBps = 0;
	s_otaElapsedMs = 0;
	startTick = xTaskGetTickCount();
	httpCfg.url = activeUrl;
	ESP_LOGI(TAG, "OTA trying URL: %s", activeUrl);

	client = esp_http_client_init(&httpCfg);
	if (!client) {
		s_otaState = OTA_STATE_FAILED;
		s_otaLastErr = ESP_ERR_NO_MEM;
		goto done;
	}

	err = esp_http_client_open(client, 0);
	if (err != ESP_OK) {
		int sockErr = errno;
		ESP_LOGE(TAG, "HTTP open failed err=%d errno=%d", err, sockErr);
		s_otaState = OTA_STATE_FAILED;
		s_otaLastErr = (err != ESP_OK) ? err : sockErr;
		goto cleanup_and_retry;
	}
	{
		int contentLen = esp_http_client_fetch_headers(client);
		if (contentLen > 0) {
			s_otaTotalBytes = contentLen;
		}
	}

	err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
	if (err != ESP_OK) {
		s_otaState = OTA_STATE_FAILED;
		s_otaLastErr = err;
		goto cleanup_and_retry;
	}

	while (1) {
		int readLen = esp_http_client_read(client, buf, sizeof(buf));
		if (readLen < 0) {
			ESP_LOGE(TAG, "HTTP read failed: %d", readLen);
			s_otaState = OTA_STATE_FAILED;
			s_otaLastErr = readLen;
			break;
		}
		if (readLen == 0) {
			break;
		}
		err = esp_ota_write(otaHandle, buf, readLen);
		if (err != ESP_OK) {
			s_otaState = OTA_STATE_FAILED;
			s_otaLastErr = err;
			break;
		}
		s_otaWrittenBytes += readLen;
		s_otaElapsedMs = (int) ((xTaskGetTickCount() - startTick) * portTICK_PERIOD_MS);
		if (s_otaElapsedMs > 0) {
			s_otaSpeedBps = (s_otaWrittenBytes * 1000) / s_otaElapsedMs;
		}
		if (s_otaTotalBytes > 0) {
			s_otaProgress = (s_otaWrittenBytes * 100) / s_otaTotalBytes;
			if (s_otaProgress > 100) {
				s_otaProgress = 100;
			}
		}
	}

	if (s_otaState != OTA_STATE_FAILED) {
		if (!esp_http_client_is_complete_data_received(client)) {
			ESP_LOGE(TAG, "OTA data incomplete written=%d total=%d", s_otaWrittenBytes, s_otaTotalBytes);
			s_otaState = OTA_STATE_FAILED;
			s_otaLastErr = ESP_FAIL;
		}
	}

	if (s_otaState != OTA_STATE_FAILED) {
		err = esp_ota_end(otaHandle);
		if (err == ESP_OK) {
			err = esp_ota_set_boot_partition(updatePartition);
		}
		if (err == ESP_OK) {
			s_otaState = OTA_STATE_SUCCESS;
			s_otaProgress = 100;
			ESP_LOGI(TAG, "OTA success, restarting...");
			vTaskDelay(pdMS_TO_TICKS(1000));
			esp_restart();
		} else {
			s_otaState = OTA_STATE_FAILED;
			s_otaLastErr = err;
		}
	}

cleanup_and_retry:
	if (client) {
		esp_http_client_close(client);
		esp_http_client_cleanup(client);
		client = NULL;
	}
	retryHttps = (strncmp(activeUrl, "https://", 8) == 0 &&
		s_otaState == OTA_STATE_FAILED &&
		httpsRetryCount < 2);
	if (retryHttps) {
		httpsRetryCount++;
		ESP_LOGW(TAG, "HTTPS OTA failed, retry %d/2 with same URL", httpsRetryCount);
		goto retry_with_url;
	}

done:
	ESP_LOGI(TAG, "OTA done state=%d err=%d", s_otaState, s_otaLastErr);
	s_otaTask = NULL;
	vTaskDelete(NULL);
}

esp_err_t ota_service_start(void)
{
	if (s_otaTask) {
		return ESP_ERR_INVALID_STATE;
	}
	if (!ota_url_valid(s_otaUrl)) {
		return ESP_ERR_INVALID_ARG;
	}
	xTaskCreate(ota_task, "ota_task", 6144, NULL, 4, &s_otaTask);
	return ESP_OK;
}

int ota_service_build_status_json(char *out, size_t outSize)
{
	return snprintf(out, outSize,
		"{\"state\":%d,\"lastErr\":%d,\"url\":\"%s\",\"progress\":%d,\"written\":%d,\"total\":%d,\"speedBps\":%d,\"elapsedMs\":%d}",
		(int) s_otaState, s_otaLastErr, s_otaUrl, s_otaProgress, s_otaWrittenBytes, s_otaTotalBytes, s_otaSpeedBps, s_otaElapsedMs);
}
