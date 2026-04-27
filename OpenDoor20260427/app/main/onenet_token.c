#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"

#include "config.h"
#include "onenet_token.h"
#include "time_sync.h"

static const char *TAG = "onenet_token";

static bool is_unreserved_char(char c)
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
		return true;
	}
	return c == '-' || c == '_' || c == '.' || c == '~';
}

static int url_encode(const char *in, char *out, size_t outSize)
{
	size_t i;
	size_t j = 0;
	for (i = 0; in[i] != '\0'; i++) {
		unsigned char c = (unsigned char) in[i];
		if (is_unreserved_char((char) c)) {
			if (j + 1 >= outSize) {
				return -1;
			}
			out[j++] = (char) c;
		} else {
			if (j + 3 >= outSize) {
				return -1;
			}
			snprintf(&out[j], outSize - j, "%%%02X", c);
			j += 3;
		}
	}
	if (j >= outSize) {
		return -1;
	}
	out[j] = '\0';
	return (int) j;
}

static bool wait_for_valid_time(int timeoutMs)
{
	int waitedMs = 0;
	while (waitedMs < timeoutMs) {
		if (time_sync_is_valid()) {
			return true;
		}
		vTaskDelay(pdMS_TO_TICKS(500));
		waitedMs += 500;
	}
	return false;
}

static void build_expire_at(char *outExpireAt, size_t outSize)
{
	time_t now = time_sync_now();
	uint64_t expire;
	uint64_t offset = (uint64_t) APP_ONENET_TOKEN_EXPIRE_YEARS * 365ULL * 24ULL * 3600ULL;

	if (now <= 1700000000 && wait_for_valid_time(15000)) {
		now = time_sync_now();
	}

	expire = ((uint64_t) (now > 0 ? now : 0)) + offset;
	ESP_LOGW(TAG, "token ts now=%lu years=%d final=%lu",
		(unsigned long) now,
		APP_ONENET_TOKEN_EXPIRE_YEARS,
		(unsigned long) expire);
	snprintf(outExpireAt, outSize, "%lu", (unsigned long) expire);
}

esp_err_t onenet_token_build(char *outToken, size_t outSize)
{
	char resource[96];
	char signInput[180];
	char expireAt[24];
	unsigned char decodedKey[96];
	size_t decodedKeyLen = 0;
	unsigned char hmacOut[32];
	size_t hmacLen = 0;
	unsigned char base64Sign[96];
	size_t base64SignLen = 0;
	char encodedResource[196];
	char encodedSign[300];
	const mbedtls_md_info_t *mdInfo;
	mbedtls_md_context_t ctx;

	snprintf(resource, sizeof(resource), "products/%s/devices/%s", APP_ONENET_PRODUCT_ID, APP_ONENET_DEVICE_NAME);
	build_expire_at(expireAt, sizeof(expireAt));
	snprintf(signInput, sizeof(signInput), "%s\n%s\n%s\n%s",
		expireAt, APP_ONENET_TOKEN_METHOD, resource, APP_ONENET_TOKEN_VERSION);

	if (mbedtls_base64_decode(decodedKey, sizeof(decodedKey), &decodedKeyLen,
		(const unsigned char *) APP_ONENET_DEVICE_KEY, strlen(APP_ONENET_DEVICE_KEY)) != 0) {
		return ESP_FAIL;
	}

	mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if (!mdInfo) {
		return ESP_FAIL;
	}
	mbedtls_md_init(&ctx);
	if (mbedtls_md_setup(&ctx, mdInfo, 1) != 0) {
		mbedtls_md_free(&ctx);
		return ESP_FAIL;
	}
	if (mbedtls_md_hmac_starts(&ctx, decodedKey, decodedKeyLen) != 0
		|| mbedtls_md_hmac_update(&ctx, (const unsigned char *) signInput, strlen(signInput)) != 0
		|| mbedtls_md_hmac_finish(&ctx, hmacOut) != 0) {
		mbedtls_md_free(&ctx);
		return ESP_FAIL;
	}
	mbedtls_md_free(&ctx);
	hmacLen = 32;
	if (mbedtls_base64_encode(base64Sign, sizeof(base64Sign), &base64SignLen, hmacOut, hmacLen) != 0) {
		return ESP_FAIL;
	}
	base64Sign[base64SignLen] = '\0';

	if (url_encode(resource, encodedResource, sizeof(encodedResource)) < 0
		|| url_encode((const char *) base64Sign, encodedSign, sizeof(encodedSign)) < 0) {
		return ESP_FAIL;
	}

	if (snprintf(outToken, outSize,
		"version=%s&res=%s&et=%s&method=%s&sign=%s",
		APP_ONENET_TOKEN_VERSION,
		encodedResource,
		expireAt,
		APP_ONENET_TOKEN_METHOD,
		encodedSign) >= (int) outSize) {
		return ESP_ERR_INVALID_SIZE;
	}
	return ESP_OK;
}
