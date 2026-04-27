#include "esp_log.h"
#include "mdns.h"

#include "config.h"
#include "mdns_service.h"

static const char *TAG = "mdns_service";
static bool s_started;

esp_err_t mdns_service_start(void)
{
	if (s_started) {
		return ESP_OK;
	}

	ESP_ERROR_CHECK(mdns_init());
	ESP_ERROR_CHECK(mdns_hostname_set(APP_MDNS_HOSTNAME));
	ESP_ERROR_CHECK(mdns_instance_name_set(APP_MDNS_INSTANCE));
	ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));

	s_started = true;
	ESP_LOGI(TAG, "mDNS started: http://%s.local/", APP_MDNS_HOSTNAME);
	return ESP_OK;
}

void mdns_service_stop(void)
{
	if (!s_started) {
		return;
	}
	mdns_free();
	s_started = false;
	ESP_LOGI(TAG, "mDNS stopped");
}
