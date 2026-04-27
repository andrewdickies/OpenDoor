#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "lwip/apps/sntp.h"

#include "time_sync.h"

static const char *TAG = "time_sync";
static bool s_started;

void time_sync_start_once(void)
{
	if (s_started) {
		return;
	}
	setenv("TZ", "CST-8", 1);
	tzset();
	sntp_stop();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "ntp.aliyun.com");
	sntp_setservername(1, "ntp1.aliyun.com");
	sntp_setservername(2, "pool.ntp.org");
	sntp_init();
	s_started = true;
	ESP_LOGI(TAG, "SNTP time sync started");
}

bool time_sync_is_valid(void)
{
	time_t now = time(NULL);
	return now > 1700000000;
}

time_t time_sync_now(void)
{
	return time(NULL);
}

void time_sync_format_local(char *out, size_t outSize)
{
	time_t now = time(NULL);
	struct tm tmLocal;

	if (!out || outSize == 0) {
		return;
	}
	if (now > 0 && localtime_r(&now, &tmLocal)) {
		snprintf(out, outSize, "%04d-%02d-%02d %02d:%02d:%02d UTC+8",
			tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday,
			tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec);
	} else {
		strlcpy(out, "unsynced", outSize);
	}
}
