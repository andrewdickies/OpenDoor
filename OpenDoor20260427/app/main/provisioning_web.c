#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "provisioning_web.h"

static const char *TAG = "prov_web";
static httpd_handle_t s_webServer;
static provisioning_apply_cb_t s_applyCb;

static const char s_htmlPage[] =
	"<!doctype html><html><head><meta charset='utf-8'>"
	"<meta name='viewport' content='width=device-width,initial-scale=1'>"
	"<title>OpenDoor Provision</title>"
	"<style>"
	"*{box-sizing:border-box;}"
	"body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#f3f4f6;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:12px;}"
	".card{width:min(520px,100%);background:#fff;border-radius:16px;padding:18px;box-shadow:0 10px 30px rgba(0,0,0,.08);}"
	"h2{margin:0 0 14px 0;font-size:22px;}"
	".desc{margin:0 0 14px 0;color:#4b5563;font-size:14px;line-height:1.5;}"
	".field{display:flex;flex-direction:column;gap:6px;margin-bottom:12px;}"
	"label{font-size:14px;color:#111827;font-weight:600;}"
	"input{width:100%;padding:10px 12px;border:1px solid #d1d5db;border-radius:10px;font-size:14px;outline:none;}"
	"input:focus{border-color:#2563eb;box-shadow:0 0 0 3px rgba(37,99,235,.15);}"
	"button{width:100%;border:none;background:#2563eb;color:#fff;border-radius:10px;padding:10px 14px;font-size:14px;font-weight:600;cursor:pointer;}"
	"button:hover{background:#1d4ed8;}"
	".tip{margin-top:10px;color:#6b7280;font-size:13px;}"
	"</style></head><body>"
	"<div class='card'>"
	"<h2>OpenDoor 配网</h2>"
	"<p class='desc'>请输入 2.4G WiFi 的名称和密码，保存后设备会自动连接。</p>"
	"<form method='POST' action='/wifi'>"
	"<div class='field'><label>WiFi 名称 (SSID)</label><input name='ssid' maxlength='32' required placeholder='请输入 WiFi 名称'></div>"
	"<div class='field'><label>WiFi 密码</label><input name='password' type='password' maxlength='64' placeholder='请输入 WiFi 密码'></div>"
	"<button type='submit'>保存并连接</button></form>"
	"<p class='tip'>提示：配置成功后设备将切换到目标路由器。</p>"
	"</div>"
	"</body></html>";

static int from_hex(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static void url_decode(char *str)
{
	char *src = str;
	char *dst = str;

	while (*src) {
		if (*src == '+') {
			*dst++ = ' ';
			src++;
			continue;
		}
		if (*src == '%' && isxdigit((unsigned char) src[1]) && isxdigit((unsigned char) src[2])) {
			int hi = from_hex(src[1]);
			int lo = from_hex(src[2]);
			*dst++ = (char) ((hi << 4) | lo);
			src += 3;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
}

static bool parse_form_field(const char *body, const char *key, char *out, size_t outSize)
{
	const char *pos = body;
	size_t keyLen = strlen(key);

	while (pos && *pos) {
		if ((strncmp(pos, key, keyLen) == 0) && pos[keyLen] == '=') {
			const char *valueStart = pos + keyLen + 1;
			const char *valueEnd = strchr(valueStart, '&');
			size_t valueLen = valueEnd ? (size_t) (valueEnd - valueStart) : strlen(valueStart);

			if (valueLen >= outSize) {
				valueLen = outSize - 1;
			}
			memcpy(out, valueStart, valueLen);
			out[valueLen] = '\0';
			url_decode(out);
			return true;
		}
		pos = strchr(pos, '&');
		if (pos) {
			pos++;
		}
	}
	return false;
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
	httpd_resp_set_type(req, "text/html; charset=utf-8");
	return httpd_resp_send(req, s_htmlPage, strlen(s_htmlPage));
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
	char body[200];
	char ssid[33] = {0};
	char password[65] = {0};
	int received = 0;

	if (req->content_len <= 0 || req->content_len >= (int) sizeof(body)) {
		httpd_resp_set_status(req, "400 Bad Request");
		httpd_resp_send(req, "Invalid form payload", strlen("Invalid form payload"));
		return ESP_FAIL;
	}

	while (received < req->content_len) {
		int ret = httpd_req_recv(req, body + received, req->content_len - received);
		if (ret <= 0) {
			httpd_resp_set_status(req, "500 Internal Server Error");
			httpd_resp_send(req, "Receive payload failed", strlen("Receive payload failed"));
			return ESP_FAIL;
		}
		received += ret;
	}
	body[received] = '\0';

	if (!parse_form_field(body, "ssid", ssid, sizeof(ssid))) {
		httpd_resp_set_status(req, "400 Bad Request");
		httpd_resp_send(req, "Missing ssid", strlen("Missing ssid"));
		return ESP_FAIL;
	}
	parse_form_field(body, "password", password, sizeof(password));

	if (s_applyCb) {
		ESP_ERROR_CHECK(s_applyCb(ssid, password));
	}

	httpd_resp_set_type(req, "text/html; charset=utf-8");
	{
		const char *okPage =
			"<html><body><h3>WiFi config received</h3>"
			"<p>Device is connecting, check serial logs.</p></body></html>";
		httpd_resp_send(req, okPage, strlen(okPage));
	}

	ESP_LOGI(TAG, "Received WiFi config, ssid=%s", ssid);
	return ESP_OK;
}

static httpd_uri_t s_indexUri = {
	.uri = "/",
	.method = HTTP_GET,
	.handler = index_get_handler,
	.user_ctx = NULL
};

static httpd_uri_t s_wifiUri = {
	.uri = "/wifi",
	.method = HTTP_POST,
	.handler = wifi_post_handler,
	.user_ctx = NULL
};

esp_err_t provisioning_web_start(provisioning_apply_cb_t applyCb)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	s_applyCb = applyCb;
	config.max_uri_handlers = 4;

	if (s_webServer) {
		return ESP_OK;
	}

	if (httpd_start(&s_webServer, &config) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start web server");
		return ESP_FAIL;
	}

	httpd_register_uri_handler(s_webServer, &s_indexUri);
	httpd_register_uri_handler(s_webServer, &s_wifiUri);
	ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
	return ESP_OK;
}

void provisioning_web_stop(void)
{
	if (s_webServer) {
		httpd_stop(s_webServer);
		s_webServer = NULL;
	}
}
